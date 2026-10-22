/*
 * Distort Me - macOS settings dialog (Cocoa).
 *
 * First version: map, mode and strength. The Windows dialog has every
 * setting; the remaining ones keep their last values here.
 * Not yet built or tested on a Mac.
 *
 * SPDX-License-Identifier: MIT
 */
#import <Cocoa/Cocoa.h>

#include "DistortMeUI.h"

#include <string.h>

@interface DMChooser : NSObject
@property(nonatomic, strong) NSTextField *pathField;
- (void)choose:(id)sender;
@end

@implementation DMChooser
- (void)choose:(id)sender
{
    (void)sender;
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = NO;
    panel.allowsMultipleSelection = NO;
    panel.message = @"Choose a map image";
    if ([panel runModal] == NSModalResponseOK)
        self.pathField.stringValue = panel.URL.path;
}
@end

static NSTextField *label(NSString *text, NSRect frame)
{
    NSTextField *l = [NSTextField labelWithString:text];
    l.frame = frame;
    return l;
}

bool DMShowDialog(void *parentWindow, DMSettings *s)
{
    (void)parentWindow;
    @autoreleasepool {
        NSView *view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 380, 96)];

        DMChooser *chooser = [[DMChooser alloc] init];
        NSTextField *path = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 68, 280, 24)];
        path.stringValue = [NSString stringWithUTF8String:s->mapPath];
        path.placeholderString = @"Map image";
        chooser.pathField = path;
        NSButton *browse = [NSButton buttonWithTitle:@"Choose..." target:chooser action:@selector(choose:)];
        browse.frame = NSMakeRect(288, 66, 92, 28);

        NSPopUpButton *mode = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(100, 34, 180, 26) pullsDown:NO];
        [mode addItemsWithTitles:@[ @"Displacement", @"Refraction", @"Gradient Flow",
                                     @"RGB Split", @"Slice", @"Blocks" ]];
        [mode selectItemAtIndex:(NSInteger)s->core.mode];

        NSTextField *strength = [[NSTextField alloc] initWithFrame:NSMakeRect(100, 2, 80, 24)];
        strength.stringValue = [NSString stringWithFormat:@"%g", (double)s->core.strength];

        [view addSubview:path];
        [view addSubview:browse];
        [view addSubview:label(@"Mode", NSMakeRect(0, 38, 96, 18))];
        [view addSubview:mode];
        [view addSubview:label(@"Strength (px)", NSMakeRect(0, 5, 96, 18))];
        [view addSubview:strength];

        NSAlert *alert = [[NSAlert alloc] init];
        alert.messageText = @"Distort Me";
        alert.informativeText = @"Grey (50%) in the map means no shift.";
        alert.accessoryView = view;
        [alert addButtonWithTitle:@"OK"];
        [alert addButtonWithTitle:@"Cancel"];

        if ([alert runModal] != NSAlertFirstButtonReturn)
            return false;

        DMSettings work = *s;
        dm_mode m = (dm_mode)mode.indexOfSelectedItem;
        if (m != work.core.mode)
            DMSettingsSetMode(&work, m);
        NSString *num = [strength.stringValue stringByReplacingOccurrencesOfString:@"," withString:@"."];
        if (num.length)
            work.core.strength = (float)num.doubleValue;
        const char *utf8 = path.stringValue.UTF8String;
        strncpy(work.mapPath, utf8 ? utf8 : "", DM_PATH_MAX - 1);
        work.mapPath[DM_PATH_MAX - 1] = 0;
        if (!work.mapPath[0])
            return false;
        *s = work;
        return true;
    }
}

void DMShowAbout(void *parentWindow)
{
    (void)parentWindow;
    @autoreleasepool {
        NSAlert *alert = [[NSAlert alloc] init];
        alert.messageText = [NSString stringWithFormat:@"Distort Me %d.%d.%d", DM_VERSION_MAJOR,
                                                       DM_VERSION_MINOR, DM_VERSION_PATCH];
        alert.informativeText = @"Free, image-driven distortion for Photoshop.\n"
                                @"github.com/therubyc/distort-me\n\nMIT License";
        [alert runModal];
    }
}
