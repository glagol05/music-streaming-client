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
static Window root;

int main() {
    Window win;
    XEvent ev;

    if((display = XOpenDisplay(NULL)) == NULL) {
        err(1, "Can't open display");
    }

    screen = DefaultScreen(display);
    root = RootWindow(display, screen);

    win = XCreateSimpleWindow(display, root, POSX, POSY, WIDTH, HEIGHT, BORDER, BlackPixel(display, screen), WhitePixel(display, screen));
    XMapWindow(display, win);

    while(XNextEvent(display, &ev) == 0) {

    }

    XUnmapWindow(display, win);
    XDestroyWindow(display, win);
    XCloseDisplay(display);
    
    return 0;
}