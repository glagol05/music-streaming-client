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

#define POSX 0
#define POSY 0
#define WIDTH 750
#define HEIGHT 600
#define BORDER 20
#define LINE 4

#define PORT "24896"
#define MAXDATASIZE 100


static Display* display;
static int screen;
static Visual *vis;
static Window root;
typedef void (*ButtonAction)(void);

struct App {
    Window main_win;
    Window button;
    //ActionButton connect_btn;
    int sock_fd;
    int connected;
};

struct {
    Window win;
    void (*action)(void);
} ActionButton;

typedef struct {
    Window win;
    unsigned long value;
} StateButton;

void *get_in_addr(struct sockaddr *sa) {
    if(sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in *)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

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

int connect_to_server(struct App *app, const char *host) {

    int numbytes;
    char buffer[MAXDATASIZE];
    struct addrinfo hints, *res, *p;
    char s[INET6_ADDRSTRLEN];
    int status;

    const char *message = "I want to connect";

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if((status = getaddrinfo(host, PORT, &hints, &res)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
    }

    for(p = res; p != NULL; p = p->ai_next) {
        if((app->sock_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1){
            perror("client: socket");
            continue;
        }

        inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr), s, sizeof(s));
            printf("Attempting connection to %s\n", s);

        if(connect(app->sock_fd, p->ai_addr, p->ai_addrlen) == -1) {
            perror("client: connect");
            close(app->sock_fd);
            continue;
        }
        
        app->connected = 1;
        break;
    }

    if(p == NULL) {
        fprintf(stderr, "client: failed to connect");
        //return 2;
        app->connected = 0;
    }

    send(app->sock_fd, message, strlen(message), 0);

    freeaddrinfo(res);
}

int run(struct App *app, GC gc) {

    XEvent ev;
    Window cur_win;
    int init = 0;

    for(;;) {
        XNextEvent(display, &ev);

        switch(ev.type) {
        case ButtonPress:

            if(ev.xbutton.button == Button1) {
                printf("This works\n");
            }
            break;

        case Expose:
            
            if(ev.xexpose.window == app -> main_win) {
                if(app->connected == 0) {
                    char *message = "Connection was refused";
                    XDrawString(display, app->main_win, gc, 50, 50, message, strlen(message));
                } 

                if(app->connected == 1) {
                    char *message = "Connection successful!";
                    XDrawString(display, app->main_win, gc, 50, 50, message, strlen(message));
                }
            }
            break;
        }
    }
}

int main(int argc, int **argv[]) {
    Window win;
    XEvent ev;
    GC gc;

    struct App app = {0};

    if((display = XOpenDisplay(NULL)) == NULL) {
        err(1, "Can't open display");
    }

    screen = DefaultScreen(display);
    root = RootWindow(display, screen);
    vis = DefaultVisual(display, screen);

    app.main_win = create_win(POSX, POSY, WIDTH, HEIGHT, BORDER, root, ExposureMask | KeyPressMask | ButtonPressMask | ButtonReleaseMask, WhitePixel(display, screen));
    app.button   = create_win(100, 100, 100, 100, 30, app.main_win, ExposureMask, WhitePixel(display, screen));

    XMapWindow(display, app.main_win);
    XMapWindow(display, app.button);

    gc = create_gc(LINE);

    connect_to_server(&app, argv[1]);

    run(&app, gc);

    XUnmapWindow(display, app.main_win);
    XUnmapWindow(display, app.button);

    XDestroyWindow(display, app.main_win);
    XDestroyWindow(display, app.button);
    
    XCloseDisplay(display);

    close(app.sock_fd);
    
    return 0;
}