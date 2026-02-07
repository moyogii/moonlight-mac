#include "macosaccent.h"

#import <AppKit/AppKit.h>

#include <QColor>

namespace MacOSAppearance {

QString getControlAccentColorHex()
{
    @autoreleasepool {
        NSColor* accentColor = [NSColor controlAccentColor];
        if (accentColor == nil) {
            return "#0A84FF";
        }

        NSColor* rgbColor = [accentColor colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];
        if (rgbColor == nil) {
            rgbColor = [accentColor colorUsingColorSpace:[NSColorSpace genericRGBColorSpace]];
        }
        if (rgbColor == nil) {
            return "#0A84FF";
        }

        CGFloat red = 0.0;
        CGFloat green = 0.0;
        CGFloat blue = 0.0;
        CGFloat alpha = 0.0;
        [rgbColor getRed:&red green:&green blue:&blue alpha:&alpha];

        return QColor::fromRgbF(red, green, blue, alpha).name(QColor::HexRgb);
    }
}

}
