#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QIcon>
#include <QQuickStyle>
#include <QMutex>
#include <QtDebug>
#include <QNetworkProxyFactory>
#include <QPalette>
#include <QFont>
#include <QCursor>
#include <QElapsedTimer>
#include <QRegularExpression>

#ifdef Q_OS_UNIX
#include <sys/socket.h>
#include <signal.h>
#endif

// Don't let SDL hook our main function, since Qt is already
// doing the same thing. This needs to be before any headers
// that might include SDL.h themselves.
#define SDL_MAIN_HANDLED
#include "SDL_compat.h"

#ifdef HAVE_FFMPEG
#include "streaming/video/ffmpeg.h"
#endif

#include "cli/listapps.h"
#include "cli/quitstream.h"
#include "cli/startstream.h"
#include "cli/pair.h"
#include "cli/commandlineparser.h"
#include "path.h"
#include "utils.h"
#include "gui/computermodel.h"
#include "gui/appmodel.h"
#include "backend/autoupdatechecker.h"
#include "backend/computermanager.h"
#include "backend/systemproperties.h"
#include "streaming/session.h"
#include "settings/streamingpreferences.h"
#include "gui/sdlgamepadkeynavigation.h"

// Log to file for both debug and release Mac builds
#define LOG_TO_FILE

// StreamUtils::setAsyncLogging() exposes control of this to the Session
// class to enable async logging once the stream has started.
//
// FIXME: Clean this up
QAtomicInt g_AsyncLoggingEnabled;

static QElapsedTimer s_LoggerTime;
static QTextStream s_LoggerStream(stderr);
static QThreadPool s_LoggerThread;
static QMutex s_SyncLoggerMutex;
static bool s_SuppressVerboseOutput;
static QRegularExpression k_RikeyRegex("&rikey=\\w+");
static QRegularExpression k_RikeyIdRegex("&rikeyid=[\\d-]+");
#ifdef LOG_TO_FILE
// Max log file size of 10 MB
static const uint64_t k_MaxLogSizeBytes = 10 * 1024 * 1024;
static QAtomicInteger<uint64_t> s_LogBytesWritten = 0;
static QFile* s_LoggerFile;
#endif

class LoggerTask : public QRunnable
{
public:
    LoggerTask(const QString& msg) : m_Msg(msg)
    {
        setAutoDelete(true);
    }

    void run() override
    {
        // QTextStream is not thread-safe, so we must lock. This will generally
        // only contend in synchronous logging mode or during a transition
        // between synchronous and asynchronous. Asynchronous won't contend in
        // the common case because we only have a single logging thread.
        QMutexLocker locker(&s_SyncLoggerMutex);
        s_LoggerStream << m_Msg;
        s_LoggerStream.flush();
    }

private:
    QString m_Msg;
};

void logToLoggerStream(QString& message)
{
    // Strip session encryption keys and IVs from the logs
    message.replace(k_RikeyRegex, "&rikey=REDACTED");
    message.replace(k_RikeyIdRegex, "&rikeyid=REDACTED");

#ifdef LOG_TO_FILE
    auto oldLogSize = s_LogBytesWritten.fetchAndAddRelaxed(message.size());
    if (oldLogSize >= k_MaxLogSizeBytes) {
        return;
    }
    else if (oldLogSize >= k_MaxLogSizeBytes - message.size()) {
        // Write one final message
        message = "Log size limit reached!";
    }
#endif

    if (g_AsyncLoggingEnabled) {
        // Queue the log message to be written asynchronously
        s_LoggerThread.start(new LoggerTask(message));
    }
    else {
        // Log the message immediately
        LoggerTask(message).run();
    }
}

void sdlLogToDiskHandler(void*, int category, SDL_LogPriority priority, const char* message)
{
    QString priorityTxt;

    switch (priority) {
    case SDL_LOG_PRIORITY_VERBOSE:
        if (s_SuppressVerboseOutput) {
            return;
        }
        priorityTxt = "Verbose";
        break;
    case SDL_LOG_PRIORITY_DEBUG:
        if (s_SuppressVerboseOutput) {
            return;
        }
        priorityTxt = "Debug";
        break;
    case SDL_LOG_PRIORITY_INFO:
        if (s_SuppressVerboseOutput) {
            return;
        }
        priorityTxt = "Info";
        break;
    case SDL_LOG_PRIORITY_WARN:
        if (s_SuppressVerboseOutput) {
            return;
        }
        priorityTxt = "Warn";
        break;
    case SDL_LOG_PRIORITY_ERROR:
        priorityTxt = "Error";
        break;
    case SDL_LOG_PRIORITY_CRITICAL:
        priorityTxt = "Critical";
        break;
    default:
        priorityTxt = "Unknown";
        break;
    }

    QTime logTime = QTime::fromMSecsSinceStartOfDay(s_LoggerTime.elapsed());
    QString txt = QString("%1 - SDL %2 (%3): %4\n").arg(logTime.toString()).arg(priorityTxt).arg(category).arg(message);

    logToLoggerStream(txt);
}

void qtLogToDiskHandler(QtMsgType type, const QMessageLogContext&, const QString& msg)
{
    QString typeTxt;

    switch (type) {
    case QtDebugMsg:
        if (s_SuppressVerboseOutput) {
            return;
        }
        typeTxt = "Debug";
        break;
    case QtInfoMsg:
        if (s_SuppressVerboseOutput) {
            return;
        }
        typeTxt = "Info";
        break;
    case QtWarningMsg:
        if (s_SuppressVerboseOutput) {
            return;
        }
        typeTxt = "Warning";
        break;
    case QtCriticalMsg:
        typeTxt = "Critical";
        break;
    case QtFatalMsg:
        typeTxt = "Fatal";
        break;
    }

    QTime logTime = QTime::fromMSecsSinceStartOfDay(s_LoggerTime.elapsed());
    QString txt = QString("%1 - Qt %2: %3\n").arg(logTime.toString()).arg(typeTxt).arg(msg);

    logToLoggerStream(txt);
}

#ifdef HAVE_FFMPEG

void ffmpegLogToDiskHandler(void* ptr, int level, const char* fmt, va_list vl)
{
    char lineBuffer[1024];
    static int printPrefix = 1;

    if ((level & 0xFF) > av_log_get_level()) {
        return;
    }
    else if ((level & 0xFF) > AV_LOG_WARNING && s_SuppressVerboseOutput) {
        return;
    }

    // We need to use the *previous* printPrefix value to determine whether to
    // print the prefix this time. av_log_format_line() will set the printPrefix
    // value to indicate whether the prefix should be printed *next time*.
    bool shouldPrefixThisMessage = printPrefix != 0;

    av_log_format_line(ptr, level, fmt, vl, lineBuffer, sizeof(lineBuffer), &printPrefix);

    if (shouldPrefixThisMessage) {
        QTime logTime = QTime::fromMSecsSinceStartOfDay(s_LoggerTime.elapsed());
        QString txt = QString("%1 - FFmpeg: %2").arg(logTime.toString()).arg(lineBuffer);
        logToLoggerStream(txt);
    }
    else {
        QString txt = QString(lineBuffer);
        logToLoggerStream(txt);
    }
}

#endif

#ifdef Q_OS_UNIX

static int signalFds[2];

void handleSignal(int sig)
{
    send(signalFds[0], &sig, sizeof(sig), 0);
}

int SDLCALL signalHandlerThread(void* data)
{
    Q_UNUSED(data);

    int sig;
    while (recv(signalFds[1], &sig, sizeof(sig), MSG_WAITALL) == sizeof(sig)) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Received signal: %d", sig);

        Session* session;
        switch (sig) {
        case SIGINT:
        case SIGTERM:
            // Check if we have an active streaming session
            session = Session::get();
            if (session != nullptr) {
                if (sig == SIGTERM) {
                    // If this is a SIGTERM, set the flag to quit
                    session->setShouldExit();
                }

                // Stop the streaming session
                session->interrupt();
            }
            else {
                // If we're not streaming, we'll close the whole app
                QCoreApplication::instance()->quit();
            }
            break;

        default:
            Q_UNREACHABLE();
        }
    }

    return 0;
}

void configureSignalHandlers()
{
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, signalFds) == -1) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "socketpair() failed: %d",
                     errno);
        return;
    }

    // Create a thread to handle our signals safely outside of signal context
    SDL_Thread* thread = SDL_CreateThread(signalHandlerThread, "Signal Handler", nullptr);
    SDL_DetachThread(thread);

    struct sigaction sa = {};
    sa.sa_handler = handleSignal;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
}

#endif

int main(int argc, char *argv[])
{
    SDL_SetMainReady();

    // Set the app version for the QCommandLineParser's showVersion() command
    QCoreApplication::setApplicationVersion(VERSION_STR);

    // Set these here to allow us to use the default QSettings constructor.
    // These also ensure that our cache directory is named correctly. As such,
    // it is critical that these be called before Path::initialize().
    QCoreApplication::setOrganizationName("Moonlight Game Streaming Project");
    QCoreApplication::setOrganizationDomain("moonlight-stream.com");
    QCoreApplication::setApplicationName("Moonlight");

    if (QFile(QDir::currentPath() + "/portable.dat").exists()) {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QDir::currentPath());
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, QDir::currentPath());

        // Initialize paths for portable mode
        Path::initialize(true);
    }
    else {
        // Initialize paths for standard installation
        Path::initialize(false);
    }

    // Override the default QML cache directory with the one we chose
    if (qEnvironmentVariableIsEmpty("QML_DISK_CACHE_PATH")) {
        qputenv("QML_DISK_CACHE_PATH", Path::getQmlCacheDir().toUtf8());
    }

#ifdef LOG_TO_FILE
    QDir tempDir(Path::getLogDir());

    s_LoggerFile = new QFile(tempDir.filePath(QString("Moonlight-%1.log").arg(QDateTime::currentSecsSinceEpoch())));
    if (s_LoggerFile->open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream(stderr) << "Redirecting log output to " << s_LoggerFile->fileName() << Qt::endl;
        s_LoggerStream.setDevice(s_LoggerFile);
    }
#endif

    // Serialize log messages on a single thread
    s_LoggerThread.setMaxThreadCount(1);
    s_LoggerTime.start();

    // Register our logger with all libraries
#if SDL_VERSION_ATLEAST(3, 0, 0)
    SDL_SetLogOutputFunction(sdlLogToDiskHandler, nullptr);
#else
    SDL_LogOutputFunction oldSdlLogFn;
    void* oldSdlLogUserdata;
    SDL_LogGetOutputFunction(&oldSdlLogFn, &oldSdlLogUserdata);
    SDL_LogSetOutputFunction(sdlLogToDiskHandler, nullptr);
#endif
    qInstallMessageHandler(qtLogToDiskHandler);
#ifdef HAVE_FFMPEG
    av_log_set_callback(ffmpegLogToDiskHandler);
#endif

#ifdef LOG_TO_FILE
    // Prune the oldest existing logs if there are more than 10
    QStringList existingLogNames = tempDir.entryList(QStringList("Moonlight-*.log"), QDir::NoFilter, QDir::SortFlag::Time);
    for (int i = 10; i < existingLogNames.size(); i++) {
        qInfo() << "Removing old log file:" << existingLogNames.at(i);
        QFile(tempDir.filePath(existingLogNames.at(i))).remove();
    }
#endif

    if (WMUtils::isRunningWindowManager()) {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        // Enable High DPI support on Qt 5.x. It is always enabled on Qt 6.0
        QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif

#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
        // Enable fractional High DPI scaling on Qt 5.14 and later
        QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
#endif
    }

#ifdef Q_OS_MACOS
    // This avoids using the default keychain for SSL, which may cause
    // password prompts on macOS.
    qputenv("QT_SSL_USE_TEMPORARY_KEYCHAIN", "1");
#endif

    // Moonlight requires the non-threaded renderer because we depend
    // on being able to control the render thread by blocking in the
    // main thread (and pumping events from the main thread when needed).
    // That doesn't work with the threaded renderer which causes all
    // sorts of odd behavior depending on the platform.
    //
    qputenv("QSG_RENDER_LOOP", "basic");

#if defined(Q_OS_DARWIN) && defined(QT_DEBUG)
    // Enable Metal validation for debug builds
    qputenv("MTL_DEBUG_LAYER", "1");
    qputenv("MTL_SHADER_VALIDATION", "1");
    
    // Enable Metal HUD overlay for debug builds
    qputenv("MTL_HUD_ENABLED", "1");
    qInfo() << "Metal HUD overlay enabled for debug build";
#endif

    // We don't want system proxies to apply to us
    QNetworkProxyFactory::setUseSystemConfiguration(false);

    // Clear any default application proxy
    QNetworkProxy noProxy(QNetworkProxy::NoProxy);
    QNetworkProxy::setApplicationProxy(noProxy);

    // Register custom metatypes for use in signals
    qRegisterMetaType<NvApp>("NvApp");

    // Allow the display to sleep by default. We will manually use SDL_DisableScreenSaver()
    // and SDL_EnableScreenSaver() when appropriate. This hint must be set before
    // initializing the SDL video subsystem to have any effect.
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");

    if (SDL_InitSubSystem(SDL_INIT_TIMER) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_InitSubSystem(SDL_INIT_TIMER) failed: %s",
                     SDL_GetError());
        return -1;
    }

#ifdef STEAM_LINK
    // Steam Link requires that we initialize video before creating our
    // QGuiApplication in order to configure the framebuffer correctly.
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_InitSubSystem(SDL_INIT_VIDEO) failed: %s",
                     SDL_GetError());
        return -1;
    }
#endif

    // Use atexit() to ensure SDL_Quit() is called. This avoids
    // racing with object destruction where SDL may be used.
    atexit(SDL_Quit);

    // Avoid the default behavior of changing the timer resolution to 1 ms.
    // We don't want this all the time that Moonlight is open. We will set
    // it manually when we start streaming.
    SDL_SetHint(SDL_HINT_TIMER_RESOLUTION, "0");

    // Disable minimize on focus loss by default. Users seem to want this off by default.
    SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");

    // SDL 2.0.12 changes the default behavior to use the button label rather than the button
    // position as most other software does. Set this back to 0 to stay consistent with prior
    // releases of Moonlight.
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0");

    // Disable relative mouse scaling to renderer size or logical DPI. We want to send
    // the mouse motion exactly how it was given to us.
    SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_SCALING, "0");

    // Set our app name for SDL to use with PulseAudio and PipeWire. This matches what we
    // provide as our app name to libsoundio too. On SDL 2.0.18+, SDL_APP_NAME is also used
    // for screensaver inhibitor reporting.
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_APP_NAME, "Moonlight");
    SDL_SetHint(SDL_HINT_APP_NAME, "Moonlight");

    // We handle capturing the mouse ourselves when it leaves the window, so we don't need
    // SDL doing it for us behind our backs.
    SDL_SetHint(SDL_HINT_MOUSE_AUTO_CAPTURE, "0");

    QGuiApplication app(argc, argv);

#ifdef Q_OS_UNIX
    // Register signal handlers to arbitrate between SDL and Qt.
    // NB: This has to be done after the QGuiApplication is constructed to
    // ensure Qt has already installed its VT signals before we override
    // some of them with our own.
    configureSignalHandlers();
#endif

    GlobalCommandLineParser parser;
    GlobalCommandLineParser::ParseResult commandLineParserResult = parser.parse(app.arguments());
    switch (commandLineParserResult) {
    case GlobalCommandLineParser::ListRequested:
        // Don't log to the console since it will jumble the command output
        s_SuppressVerboseOutput = true;
        break;
    default:
        break;
    }

    SDL_version compileVersion;
    SDL_VERSION(&compileVersion);
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Compiled with SDL %d.%d.%d",
                compileVersion.major, compileVersion.minor, compileVersion.patch);

    SDL_version runtimeVersion;
    SDL_GetVersion(&runtimeVersion);
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Running with SDL %d.%d.%d",
                runtimeVersion.major, runtimeVersion.minor, runtimeVersion.patch);

    // Apply the initial translation based on user preference
    StreamingPreferences::get()->retranslate();


    // Trickily declare the translation for dialog buttons
    QCoreApplication::translate("QPlatformTheme", "&Yes");
    QCoreApplication::translate("QPlatformTheme", "&No");
    QCoreApplication::translate("QPlatformTheme", "OK");
    QCoreApplication::translate("QPlatformTheme", "Help");
    QCoreApplication::translate("QPlatformTheme", "Cancel");

#ifdef STEAM_LINK
    // Qt 5.9 from the Steam Link SDK is not able to load any fonts
    // since the Steam Link doesn't include any of the ones it looks
    // for. We know it has NotoSans so we will explicitly ask for that.
    if (app.font().family().isEmpty()) {
        qWarning() << "SL HACK: No default font - using NotoSans";

        QFont fon("NotoSans");
        app.setFont(fon);
    }

    // Move the mouse to the bottom right so it's invisible when using
    // gamepad-only navigation.
    QCursor().setPos(0xFFFF, 0xFFFF);
#endif

    // Keep macOS app bundle icon from Info.plist/iconset.

    // Register our C++ types for QML
    qmlRegisterType<ComputerModel>("ComputerModel", 1, 0, "ComputerModel");
    qmlRegisterType<AppModel>("AppModel", 1, 0, "AppModel");
    qmlRegisterUncreatableType<Session>("Session", 1, 0, "Session", "Session cannot be created from QML");
    qmlRegisterSingletonType<ComputerManager>("ComputerManager", 1, 0,
                                              "ComputerManager",
                                              [](QQmlEngine* qmlEngine, QJSEngine*) -> QObject* {
                                                  return new ComputerManager(StreamingPreferences::get(qmlEngine));
                                              });
    qmlRegisterSingletonType<AutoUpdateChecker>("AutoUpdateChecker", 1, 0,
                                                "AutoUpdateChecker",
                                                [](QQmlEngine*, QJSEngine*) -> QObject* {
                                                    return new AutoUpdateChecker();
                                                });
    qmlRegisterSingletonType<SystemProperties>("SystemProperties", 1, 0,
                                               "SystemProperties",
                                               [](QQmlEngine*, QJSEngine*) -> QObject* {
                                                   return new SystemProperties();
                                               });
    qmlRegisterSingletonType<SdlGamepadKeyNavigation>("SdlGamepadKeyNavigation", 1, 0,
                                                      "SdlGamepadKeyNavigation",
                                                      [](QQmlEngine* qmlEngine, QJSEngine*) -> QObject* {
                                                          return new SdlGamepadKeyNavigation(StreamingPreferences::get(qmlEngine));
                                                      });
    qmlRegisterSingletonType<StreamingPreferences>("StreamingPreferences", 1, 0,
                                                   "StreamingPreferences",
                                                   [](QQmlEngine* qmlEngine, QJSEngine*) -> QObject* {
                                                       return StreamingPreferences::get(qmlEngine);
                                                   });

    // Create the identity manager on the main thread
    IdentityManager::get();

    // We require the Material theme
    QQuickStyle::setStyle("Material");

    // Our icons are styled for a dark theme, so we do not allow the user to override this
    qputenv("QT_QUICK_CONTROLS_MATERIAL_THEME", "Dark");

    // Accent is sourced from macOS system appearance in QML.
    // Ignore any user-provided Material accent environment override.
    qunsetenv("QT_QUICK_CONTROLS_MATERIAL_ACCENT");

    // These are defaults that we allow the user to override
    if (!qEnvironmentVariableIsSet("QT_QUICK_CONTROLS_MATERIAL_VARIANT")) {
        qputenv("QT_QUICK_CONTROLS_MATERIAL_VARIANT", "Dense");
    }
    if (!qEnvironmentVariableIsSet("QT_QUICK_CONTROLS_MATERIAL_PRIMARY")) {
        // Qt 6.9 began to use a different shade of Material.Indigo when we use a dark theme
        // (which is all the time). The new color looks washed out, so manually specify the
        // old primary color unless the user overrides it themselves.
        qputenv("QT_QUICK_CONTROLS_MATERIAL_PRIMARY", "#3F51B5");
    }

    QQmlApplicationEngine engine;
    QString initialView;
    bool hasGUI = true;

    switch (commandLineParserResult) {
    case GlobalCommandLineParser::NormalStartRequested:
        initialView = "qrc:/gui/PcView.qml";
        break;
    case GlobalCommandLineParser::StreamRequested:
        {
            initialView = "qrc:/gui/CliStartStreamSegue.qml";
            StreamingPreferences* preferences = StreamingPreferences::get();
            StreamCommandLineParser streamParser;
            streamParser.parse(app.arguments(), preferences);
            QString host    = streamParser.getHost();
            QString appName = streamParser.getAppName();
            auto launcher   = new CliStartStream::Launcher(host, appName, preferences, &app);
            engine.rootContext()->setContextProperty("launcher", launcher);
            break;
        }
    case GlobalCommandLineParser::QuitRequested:
        {
            initialView = "qrc:/gui/CliQuitStreamSegue.qml";
            QuitCommandLineParser quitParser;
            quitParser.parse(app.arguments());
            auto launcher = new CliQuitStream::Launcher(quitParser.getHost(), &app);
            engine.rootContext()->setContextProperty("launcher", launcher);
            break;
        }
    case GlobalCommandLineParser::PairRequested:
        {
            initialView = "qrc:/gui/CliPair.qml";
            PairCommandLineParser pairParser;
            pairParser.parse(app.arguments());
            auto launcher = new CliPair::Launcher(pairParser.getHost(), pairParser.getPredefinedPin(), &app);
            engine.rootContext()->setContextProperty("launcher", launcher);
            break;
        }
    case GlobalCommandLineParser::ListRequested:
        {
            ListCommandLineParser listParser;
            listParser.parse(app.arguments());
            auto launcher = new CliListApps::Launcher(listParser.getHost(), listParser, &app);
            launcher->execute(new ComputerManager(StreamingPreferences::get()));
            hasGUI = false;
            break;
        }
    }

    if (hasGUI) {
        engine.rootContext()->setContextProperty("initialView", initialView);
        engine.rootContext()->setContextProperty("runConfigChecks", commandLineParserResult == GlobalCommandLineParser::NormalStartRequested);

        // Load the main.qml file
        engine.load(QUrl(QStringLiteral("qrc:/gui/main.qml")));
        if (engine.rootObjects().isEmpty())
            return -1;
    }

    int err = app.exec();

    // Give worker tasks time to properly exit. Fixes PendingQuitTask
    // sometimes freezing and blocking process exit.
    QThreadPool::globalInstance()->waitForDone(30000);

    // Restore the default logger for all libraries before shutting down ours
#if SDL_VERSION_ATLEAST(3, 0, 0)
    SDL_SetLogOutputFunction(SDL_GetDefaultLogOutputFunction(), nullptr);
#else
    SDL_LogSetOutputFunction(oldSdlLogFn, oldSdlLogUserdata);
#endif
    qInstallMessageHandler(nullptr);
#ifdef HAVE_FFMPEG
    av_log_set_callback(av_log_default_callback);
#endif

    // We should not be in async logging mode anymore
    Q_ASSERT(g_AsyncLoggingEnabled == 0);

    // Wait for pending log messages to be printed
    s_LoggerThread.waitForDone();

    return err;
}
