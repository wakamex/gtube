// The sign-in window on macOS: WebKit's WKWebView in a window of the player's own process, as
// WebView2 on Windows (AppKit is loaded already, and WebKit runs each page in processes of its own).
// Its cookie store lives only as long as the window, like the other systems' sessions.
#import <AppKit/AppKit.h>
#import <WebKit/WebKit.h>

#include "signin.h"
#include "signin_jar.h"

@interface GtubeSignin : NSObject <WKNavigationDelegate, NSWindowDelegate>
@property(nonatomic, strong) NSWindow *window;
@property(nonatomic, strong) WKWebView *view;
@property(nonatomic) BOOL asking;  // a cookie request is out
@property(nonatomic) int result;   // 0 signing in, 1 signed in, -1 closed
@property(nonatomic) char *jar;    // the session, once signed in
@end

@implementation GtubeSignin

// Signed in once Google sends the page on to YouTube: then the cookies are read.
- (void)webView:(WKWebView *)view didFinishNavigation:(WKNavigation *)navigation {
    NSString *url = view.URL.absoluteString;
    if (self.asking || self.result || !([url hasPrefix:@"https://music.youtube.com/"] || [url hasPrefix:@"https://www.youtube.com/"])) return;
    self.asking = YES;
    __weak GtubeSignin *weak = self;  // (the window may be closed before the cookies arrive)
    [view.configuration.websiteDataStore.httpCookieStore getAllCookies:^(NSArray<NSHTTPCookie *> *cookies) {
        GtubeSignin *me = weak;
        if (!me) return;
        me.asking = NO;
        jar j = jar_new();
        for (NSHTTPCookie *c in cookies)
            jar_add(&j, c.domain.UTF8String, c.path.UTF8String, c.secure, c.HTTPOnly,
                    c.expiresDate ? (long long)c.expiresDate.timeIntervalSince1970 : 0, c.name.UTF8String, c.value.UTF8String);
        SDL_Log("sign-in: %u cookies, %s", (unsigned)cookies.count, j.youtube_session ? "signed in to YouTube" : "not signed in yet");
        if (j.youtube_session && !me.result) me.jar = j.text, me.result = 1;
        else SDL_free(j.text);
    }];
}

- (void)windowWillClose:(NSNotification *)notification {
    if (!self.result) self.result = -1;
}

@end

struct signin {
    void *ref;  // the GtubeSignin, retained
};

static GtubeSignin *get(signin *s) { return (__bridge GtubeSignin *)s->ref; }

signin *signin_open(const char *data_dir, char *why, size_t size) {
    (void)data_dir, (void)why, (void)size;
    GtubeSignin *g = [GtubeSignin new];
    WKWebViewConfiguration *config = [WKWebViewConfiguration new];
    config.websiteDataStore = [WKWebsiteDataStore nonPersistentDataStore];
    // WKWebView's own user agent leaves out Safari's name, which Google's sign-in may refuse as an
    // embedded browser; with it, the page sees the Safari this WebKit is.
    config.applicationNameForUserAgent = @"Version/18.0 Safari/605.1.15";
    NSRect frame = NSMakeRect(0, 0, 520, 720);
    g.view = [[WKWebView alloc] initWithFrame:frame configuration:config];
    g.view.navigationDelegate = g;
    g.window = [[NSWindow alloc] initWithContentRect:frame
                                           styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                                             backing:NSBackingStoreBuffered
                                               defer:NO];
    g.window.releasedWhenClosed = NO;
    g.window.title = @"Sign in to YouTube Music";
    g.window.contentView = g.view;
    g.window.delegate = g;
    [g.window center];
    [g.view loadRequest:[NSURLRequest requestWithURL:[NSURL URLWithString:@SIGNIN_URL]]];
    [g.window makeKeyAndOrderFront:nil];
    signin *s = SDL_calloc(1, sizeof *s);
    s->ref = (void *)CFBridgingRetain(g);
    return s;
}

void signin_event(signin *s, const SDL_Event *e) { (void)s, (void)e; }  // (the window is AppKit's, not SDL's)

int signin_poll(signin *s, char **jar, char *why, size_t size) {
    if (!s) return -1;
    GtubeSignin *g = get(s);
    if (g.result == 1 && jar) *jar = g.jar, g.jar = NULL;
    if (g.result == -1) SDL_strlcpy(why, "Sign-in closed", size);
    return g.result;
}

void signin_close(signin *s) {
    if (!s) return;
    GtubeSignin *g = (__bridge_transfer GtubeSignin *)s->ref;  // (released at the end of this function)
    g.window.delegate = nil;
    g.view.navigationDelegate = nil;
    [g.window close];
    SDL_free(g.jar);
    g.jar = NULL;
    SDL_free(s);
}
