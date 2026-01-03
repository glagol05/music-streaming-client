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
#define WIDTH 1200
#define HEIGHT 750
#define BORDER 20
#define LINE 4

#define PORT "24896"
#define MAXDATASIZE 100

#define MAX_ARTISTS 15
#define MAX_ALBUMS 5
#define MAX_SONGS 8

#define ROW_HEIGHT 30

int artist_scroll = 0;
int album_scroll  = 0;
int song_scroll   = 0;


static Display* display;
static int screen;
static Visual *vis;
static Window root;
typedef void (*ButtonAction)(void);

struct App {
    Window main_win;
    Window button;
    Window artist_win;
    Window album_win;
    Window song_win;
    //ActionButton connect_btn;
    int sock_fd;
    int connected;
};

typedef struct {
    char *title;
    int duration;
} Song;

typedef struct {
    char *title;
    int num_songs;
    Song *songs;
} Album;

typedef struct {
    char *name;
    int num_albums;
    Album *albums;
    int num_songs;
} Artist;

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

int run(struct App *app, GC gc, Artist artists[], int num_artists) {

    XEvent ev;
    Window cur_win;
    int init = 0;


    for (;;) {
        XNextEvent(display, &ev);

        switch (ev.type) {
        case ButtonPress:
            if (ev.xbutton.button == Button1) {
                printf("Click!\n");
            }

            if (ev.xbutton.window == app->artist_win) {
                if (ev.xbutton.button == Button4) artist_scroll -= ROW_HEIGHT;
                if (ev.xbutton.button == Button5) artist_scroll += ROW_HEIGHT;

                XWindowAttributes attr;
                XGetWindowAttributes(display, app->artist_win, &attr);
                int win_height = attr.height;
                int max_scroll = num_artists * ROW_HEIGHT - win_height;
                if (max_scroll < 0) max_scroll = 0;

                if (artist_scroll < 0) artist_scroll = 0;
                if (artist_scroll > max_scroll) artist_scroll = max_scroll;

                XClearWindow(display, app->artist_win);
                for (int i = 0; i < num_artists; i++) {
                    int y = i * ROW_HEIGHT - artist_scroll;
                    if (y + ROW_HEIGHT < 0 || y > win_height) continue;
                    XDrawString(display, app->artist_win, gc, 5, y + 20, artists[i].name, strlen(artists[i].name));
                }
            }

            if (ev.xbutton.window == app->album_win) {
                if (ev.xbutton.button == Button4) album_scroll -= ROW_HEIGHT;
                if (ev.xbutton.button == Button5) album_scroll += ROW_HEIGHT;

                XWindowAttributes attr;
                XGetWindowAttributes(display, app->album_win, &attr);
                int win_height = attr.height;
                int max_scroll = artists[0].num_albums * ROW_HEIGHT - win_height;
                if (max_scroll < 0) max_scroll = 0;

                if (album_scroll < 0) album_scroll = 0;
                if (album_scroll > max_scroll) album_scroll = max_scroll;

                XClearWindow(display, app->album_win);
                for (int i = 0; i < artists[0].num_albums; i++) {
                    int y = i * ROW_HEIGHT - album_scroll;
                    if (y + ROW_HEIGHT < 0 || y > win_height) continue;
                    XDrawString(display, app->album_win, gc, 5, y + 20, artists[0].albums[i].title, strlen(artists[0].albums[i].title));
                }
            }

            if (ev.xbutton.window == app->song_win) {
                if (ev.xbutton.button == Button4) song_scroll -= ROW_HEIGHT;
                if (ev.xbutton.button == Button5) song_scroll += ROW_HEIGHT;

                XWindowAttributes attr;
                XGetWindowAttributes(display, app->song_win, &attr);
                int win_height = attr.height;
                int max_scroll = artists[0].albums[0].num_songs * ROW_HEIGHT - win_height;
                if (max_scroll < 0) max_scroll = 0;

                if (song_scroll < 0) song_scroll = 0;
                if (song_scroll > max_scroll) song_scroll = max_scroll;

                XClearWindow(display, app->song_win);
                for (int i = 0; i < artists[0].albums[0].num_songs; i++) {
                    int y = i * ROW_HEIGHT - song_scroll;
                    if (y + ROW_HEIGHT < 0 || y > win_height) continue;
                    XDrawString(display, app->song_win, gc, 5, y + 20, artists[0].albums[0].songs[i].title, strlen(artists[0].albums[0].songs[i].title));
                }
            }
            break;

        case Expose:
            if (ev.xexpose.window == app->main_win) {
                char *message = app->connected ? "Connection successful!" : "Connection was refused";
                XDrawString(display, app->main_win, gc, 400, 350, message, strlen(message));
            }

            if (ev.xexpose.window == app->artist_win) {
                XWindowAttributes attr;
                XGetWindowAttributes(display, app->artist_win, &attr);
                int win_height = attr.height;

                XClearWindow(display, app->artist_win);
                for (int i = 0; i < num_artists; i++) {
                    int y = i * ROW_HEIGHT - artist_scroll;
                    if (y + ROW_HEIGHT < 0 || y > win_height) continue;
                    XDrawString(display, app->artist_win, gc, 5, y + 20, artists[i].name, strlen(artists[i].name));
                }
            }

            if (ev.xexpose.window == app->album_win) {
                XWindowAttributes attr;
                XGetWindowAttributes(display, app->album_win, &attr);
                int win_height = attr.height;

                XClearWindow(display, app->album_win);
                for (int i = 0; i < artists[0].num_albums; i++) {
                    int y = i * ROW_HEIGHT - album_scroll;
                    if (y + ROW_HEIGHT < 0 || y > win_height) continue;
                    XDrawString(display, app->album_win, gc, 5, y + 20, artists[0].albums[i].title, strlen(artists[0].albums[i].title));
                }
            }

            if (ev.xexpose.window == app->song_win) {
                XWindowAttributes attr;
                XGetWindowAttributes(display, app->song_win, &attr);
                int win_height = attr.height;

                XClearWindow(display, app->song_win);
                for (int i = 0; i < artists[0].albums[0].num_songs; i++) {
                    int y = i * ROW_HEIGHT - song_scroll;
                    if (y + ROW_HEIGHT < 0 || y > win_height) continue;
                    XDrawString(display, app->song_win, gc, 5, y + 20, artists[0].albums[0].songs[i].title, strlen(artists[0].albums[0].songs[i].title));
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


    Artist artists[MAX_ARTISTS];

    for(int i = 0; i < MAX_ARTISTS; i++) {
        artists[i].name = malloc(32);
        snprintf(artists[i].name, 32, "Artist %d", i+1);

        artists[i].num_albums = MAX_ALBUMS;
        artists[i].num_songs = MAX_SONGS;

        Album *albums = malloc(sizeof(Album) * MAX_ALBUMS);
        for(int j = 0; j < MAX_ALBUMS; j++) {
            albums[j].title = malloc(32);
            snprintf(albums[j].title, 32, "Album %d-%d", i+1, j+1);
            albums[j].num_songs = MAX_SONGS;

            Song *songs = malloc(sizeof(Song) * MAX_SONGS);
            for(int k = 0; k < MAX_SONGS; k++) {
                songs[k].title = malloc(32);
                snprintf(songs[k].title, 32, "Song %d-%d-%d", i+1, j+1, k+1);
                songs[k].duration = (k+1)*30;
            }
            albums[j].songs = songs;
        }
        artists[i].albums = albums;
    }

    if((display = XOpenDisplay(NULL)) == NULL) {
        err(1, "Can't open display");
    }

    screen = DefaultScreen(display);
    root = RootWindow(display, screen);
    vis = DefaultVisual(display, screen);

    app.main_win = create_win(POSX, POSY, WIDTH, HEIGHT, BORDER, root, ExposureMask | KeyPressMask | ButtonPressMask | ButtonReleaseMask, WhitePixel(display, screen));
    app.artist_win = create_win(0, 0, 400, 300, 2, app.main_win, ExposureMask | ButtonPressMask, WhitePixel(display, screen));
    app.album_win  = create_win(400, 0, 400, 300, 2, app.main_win, ExposureMask| ButtonPressMask, WhitePixel(display, screen));
    app.song_win   = create_win(800, 0, 400, 300, 2, app.main_win, ExposureMask| ButtonPressMask, WhitePixel(display, screen));
    //app.button   = create_win(0, 0, 250, 200, 10, app.main_win, ExposureMask, WhitePixel(display, screen));

    XMapWindow(display, app.main_win);
    XMapWindow(display, app.artist_win);
    XMapWindow(display, app.album_win);
    XMapWindow(display, app.song_win);

    gc = create_gc(LINE);

    connect_to_server(&app, argv[1]);

    run(&app, gc, artists, MAX_ARTISTS);

    XUnmapWindow(display, app.main_win);
    XUnmapWindow(display, app.artist_win);
    XUnmapWindow(display, app.album_win);
    XUnmapWindow(display, app.song_win);

    XDestroyWindow(display, app.main_win);
    XDestroyWindow(display, app.artist_win);
    XDestroyWindow(display, app.album_win);
    XDestroyWindow(display, app.song_win);
    
    XCloseDisplay(display);

    close(app.sock_fd);
    
    return 0;
}