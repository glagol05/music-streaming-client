#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/X.h>
#include <X11/XKBlib.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <X11/Xatom.h>
#include <X11/Xft/Xft.h>

#define POSX 500
#define POSY 500
#define WIDTH 750
#define HEIGHT 600
#define BORDER 20
#define LINE 4
#define PORT "24896"


static Display* display;
static int screen;
static Visual *vis;
static Window root;
typedef void (*ButtonAction)(void);

struct {
    Window win;
    void (*action)(void);
} ActionButton;

typedef struct {
    Window win;
    unsigned long value;
} StateButton;

static Window create_win(int x, int y, int w, int h, int b, Window parent, long event_mask, unsigned long bg_pixel) {

    XSetWindowAttributes xwa;
    xwa.background_pixel = bg_pixel;
    xwa.border_pixel = BlackPixel(display, screen);
    xwa.event_mask = event_mask;

    return XCreateWindow(
        display,
        parent,
        x, y, w, h, b,
        DefaultDepth(display, screen),
        InputOutput,
        vis,
        CWBackPixel | CWBorderPixel | CWEventMask,
        &xwa
    );
}

static GC create_gc(int line_width) {
    GC gc;
    XGCValues xgcv;
    unsigned long valuemask;

    xgcv.line_style = LineSolid;
    xgcv.line_width = line_width;
    xgcv.cap_style = CapButt;
    xgcv.join_style = JoinMiter;
    xgcv.fill_style = FillSolid;
    xgcv.foreground = BlackPixel(display, screen);
    xgcv.background = WhitePixel(display, screen);

    valuemask = GCForeground | GCBackground | GCFillStyle | GCLineStyle | GCLineWidth | GCCapStyle | GCJoinStyle;
    gc = XCreateGC(display, root, valuemask, &xgcv);
    return gc;
}

int main() {
    Window win;
    XEvent ev;
    GC gc;

    if((display = XOpenDisplay(NULL)) == NULL) {
        err(1, "Can't open display");
    }

    screen = DefaultScreen(display);
    root = RootWindow(display, screen);
    vis = DefaultVisual(display, screen);
    Window main_win = create_win(POSX, POSY, WIDTH, HEIGHT, BORDER, root, ExposureMask | KeyPressMask | ButtonPressMask | ButtonReleaseMask, WhitePixel(display, screen));

    XMapWindow(display, main_win);

    while(XNextEvent(display, &ev) == 0) {

    }

    gc = create_gc(LINE);
    XSetForeground(display, gc, BlackPixel(display, screen));

    XUnmapWindow(display, main_win);
    XDestroyWindow(display, main_win);
    XCloseDisplay(display);
    
    return 0;
}