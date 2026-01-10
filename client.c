#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <mpg123.h>
#include <alsa/asoundlib.h>
#include <signal.h>
#include <pthread.h>
#include <X11/keysym.h>

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

int selected_artist = -1;
int selected_album = -1;
int selected_song = -1;

static char *xstrdup(const char *s)
{
    size_t len = strlen(s) + 1;
    char *p = malloc(len);
    if (!p) {
        perror("malloc");
        exit(1);
    }
    memcpy(p, s, len);
    return p;
}


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
    Window musicButtons;
    Window pauseButton;
    //ActionButton connect_btn;
    int sock_fd;
    int connected;
    int num_artists;
    char *server_host;
    char *start_arg;
};

typedef struct {
    char *title;
    int duration;
    int track;
    int id;
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

struct PlaybackArgs {
    struct App *app;
    int song_id;
};

Artist artistlist[100];
volatile sig_atomic_t paused = 0;
static pthread_t playback_thread;
static int playback_running = 0;
static snd_pcm_t *global_pcm = NULL;

static int song_cmp(const void *a, const void *b) {
    const Song *sa = a;
    const Song *sb = b;

    if (sa->track == 0 && sb->track == 0)
        return sa->id - sb->id;

    if (sa->track == 0) return 1;
    if (sb->track == 0) return -1;

    return sa->track - sb->track;
}

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

int connect_to_server(struct App *app, const char *host, int firstTime) {

    int numbytes;
    char buffer[MAXDATASIZE];
    struct addrinfo hints, *res, *p;
    char s[INET6_ADDRSTRLEN];
    int status;

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

    if(firstTime == 1){
        send(app->sock_fd, app->start_arg, strlen(app->start_arg), 0);
    }

    int ok = app->connected;
    freeaddrinfo(res);
    return ok;
}

void receive_metadata(int socket, struct App *app) {
    ssize_t n;
    char recbuffer[1024];
    char linebuf[1024];
    int linepos = 0;

    char current_artist[256] = "";
    char current_album[256]  = "";
    char current_song[256]   = "";
    int current_track = 0;
    int current_id = -1;
    int  duration             = 0;

    int artist_counter = 0;

    while ((n = recv(socket, recbuffer, sizeof(recbuffer), 0)) > 0) {
        for (int i = 0; i < n; i++) {
            if (recbuffer[i] == '\n') {
                linebuf[linepos] = '\0';
                linepos = 0;

                if (strlen(linebuf) == 0)
                    continue;

                printf("DEBUG line='%s'\n", linebuf);

                char value[256];

                if (sscanf(linebuf, "ARTIST:%255[^\n]", value) == 1) {
                    strcpy(current_artist, value);
                }
                else if (sscanf(linebuf, "ALBUM:%255[^\n]", value) == 1) {
                    strcpy(current_album, value);
                }
                else if (sscanf(linebuf, "SONG:%255[^\n]", value) == 1) {
                    strcpy(current_song, value);
                } else if (sscanf(linebuf, "TRACK:%d", &current_track) == 1) {
                } else if (sscanf(linebuf, "ID:%d", &current_id) == 1) {
                }
                else if (sscanf(linebuf, "DURATION:%d", &duration) == 1) {

                    int artist_index = -1;
                    for (int a = 0; a < artist_counter; a++) {
                        if (strcmp(artistlist[a].name, current_artist) == 0) {
                            artist_index = a;
                            break;
                        }
                    }

                    if (artist_index == -1) {
                        artist_index = artist_counter;
                        artistlist[artist_counter].name = xstrdup(current_artist);
                        artistlist[artist_counter].albums = calloc(100, sizeof(Album));
                        artistlist[artist_counter].num_albums = 0;
                        artist_counter++;
                    }

                    Artist *a = &artistlist[artist_index];

                    int album_index = -1;
                    for (int j = 0; j < a->num_albums; j++) {
                        if (strcmp(a->albums[j].title, current_album) == 0) {
                            album_index = j;
                            break;
                        }
                    }

                    Album *al;

                    if (album_index == -1) {
                        album_index = a->num_albums;
                        al = &a->albums[album_index];

                        al->title = xstrdup(current_album);
                        al->songs = calloc(100, sizeof(Song));
                        al->num_songs = 0;

                        a->num_albums++;
                    } else {
                        al = &a->albums[album_index];
                    }

                    int song_index = -1;
                    for (int k = 0; k < al->num_songs; k++) {
                        if (strcmp(al->songs[k].title, current_song) == 0) {
                            song_index = k;
                            break;
                        }
                    }

                    if (song_index == -1) {
                        song_index = al->num_songs;
                        al->songs[song_index].title = xstrdup(current_song);
                        al->songs[song_index].track = current_track;
                        al->songs[song_index].id = current_id;
                        al->songs[song_index].duration = duration;
                        al->num_songs++;
                    }
                }

            } else {
                if (linepos < sizeof(linebuf) - 1)
                    linebuf[linepos++] = recbuffer[i];
            }
        }
    }

    if (n == 0)
        printf("DEBUG: connection closed by server\n");
    if (n < 0)
        perror("recv");

    for (int a = 0; a < artist_counter; a++) {
        Artist *ar = &artistlist[a];
        for (int al = 0; al < ar->num_albums; al++) {
            Album *album = &ar->albums[al];
            if (album->num_songs > 1) {
                qsort(album->songs, album->num_songs, sizeof(Song), song_cmp);
            }
        }
    }

    app->num_artists = artist_counter;
    close(app->sock_fd);
}

int request_song(int id, struct App *app) {
    if (!connect_to_server(app, app->server_host, 0))
        return -1;

    char buf[64];
    snprintf(buf, sizeof(buf), "PLAY %d\n", id);
    send(app->sock_fd, buf, strlen(buf), 0);

    return 0;
}

int receive_song(struct App *app)
{
    unsigned char buffer[8192];
    ssize_t n;

    FILE *out = fopen("song.mp3", "wb");
    if (!out) {
        perror("fopen");
        return -1;
    }

    while ((n = recv(app->sock_fd, buffer, sizeof(buffer), 0)) > 0) {
        fwrite(buffer, 1, n, out);
    }

    if (n < 0) {
        perror("recv");
        fclose(out);
        close(app->sock_fd);
        return -1;
    }

    fclose(out);
    close(app->sock_fd);
    return 0;
}


void play_song(const char *filepath)
{
    mpg123_handle *mh = NULL;
    snd_pcm_t *pcm = NULL;

    int err;
    unsigned char buffer[4096];
    size_t bytes_read;

    long rate;
    int channels, encoding;

    mh = mpg123_new(NULL, &err);
    if (!mh) {
        fprintf(stderr, "mpg123_new failed: %s\n", mpg123_plain_strerror(err));
        return;
    }

    if (mpg123_open(mh, filepath) != MPG123_OK) {
        fprintf(stderr, "mpg123_open failed: %s\n", mpg123_strerror(mh));
        goto cleanup;
    }

    if (mpg123_getformat(mh, &rate, &channels, &encoding) != MPG123_OK) {
        fprintf(stderr, "mpg123_getformat failed\n");
        goto cleanup;
    }

    mpg123_format_none(mh);
    mpg123_format(mh, rate, channels, MPG123_ENC_SIGNED_16);

    if ((err = snd_pcm_open(&pcm, "default", SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
        fprintf(stderr, "snd_pcm_open: %s\n", snd_strerror(err));
        goto cleanup;
    }

    if ((err = snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, channels, rate, 1, 100000)) < 0) {
        fprintf(stderr, "snd_pcm_set_params: %s\n", snd_strerror(err));
        goto cleanup;
    }

    while (mpg123_read(mh, buffer, sizeof(buffer), &bytes_read) == MPG123_OK) {

        if (paused) {
            snd_pcm_pause(pcm, 1);
            while (paused) {
                usleep(1000);
            }
            snd_pcm_pause(pcm, 0);
        }

        snd_pcm_sframes_t frames = bytes_read / (channels * 2);
        unsigned char *ptr = buffer;

        while (frames > 0) {
            snd_pcm_sframes_t written = snd_pcm_writei(pcm, ptr, frames);

            if (written == -EPIPE) {
                snd_pcm_prepare(pcm);
                continue;
            } else if (written < 0) {
                fprintf(stderr, "snd_pcm_writei: %s\n",
                        snd_strerror(written));
                goto cleanup;
            }

            frames -= written;
            ptr += written * channels * 2;
        }
    }

    snd_pcm_drain(pcm);

    cleanup:
        if (pcm)
            snd_pcm_close(pcm);

        if (mh) {
            mpg123_close(mh);
            mpg123_delete(mh);
        }
}

void *playback_thread_func(void *arg)
{
    struct PlaybackArgs *pa = arg;

    paused = 0;
    playback_running = 1;

    request_song(pa->song_id, pa->app);
    receive_song(pa->app);
    play_song("song.mp3");

    playback_running = 0;
    free(pa);
    return NULL;
}

void pause_song(void)  { paused = 1; }
void resume_song(void) { paused = 0; }

int run(struct App *app, GC gc, int num_artists) {

    XEvent ev;

    for (;;) {
        XNextEvent(display, &ev);

        switch (ev.type) {
        case ButtonPress:
            if (ev.xbutton.window == app->pauseButton) {
                paused = !paused;
            }

            if (ev.xbutton.button == Button1) {
                int click_y = ev.xbutton.y;
                Window win = ev.xbutton.window;

                if (win == app->artist_win) {
                    int artist_row = (click_y + artist_scroll) / ROW_HEIGHT;
                    if (artist_row >= 0 && artist_row < num_artists) {
                        selected_artist = artist_row;
                        selected_album = -1;
                        selected_song = -1;

                        XExposeEvent e;
                        memset(&e, 0, sizeof(e));
                        e.type = Expose;
                        e.display = display;

                        e.window = app->album_win;
                        XSendEvent(display, app->album_win, False, ExposureMask, (XEvent *)&e);

                        e.window = app->song_win;
                        XSendEvent(display, app->song_win, False, ExposureMask, (XEvent *)&e);
                    }
                }

                if (win == app->album_win && selected_artist != -1) {
                    int album_row = (click_y + album_scroll) / ROW_HEIGHT;
                    if (album_row >= 0 && album_row < artistlist[selected_artist].num_albums) {
                        selected_album = album_row;
                        selected_song = -1;
                        XExposeEvent e;
                        memset(&e, 0, sizeof(e));
                        e.type = Expose;
                        e.display = display;
                        e.window = app->song_win;
                        XSendEvent(display, app->song_win, False, ExposureMask, (XEvent *)&e);

                        XClearWindow(display, app->song_win);
                    }
                }

                if (win == app->song_win && selected_artist != -1 && selected_album != -1) {
                    int row = (click_y + song_scroll) / ROW_HEIGHT;
                    if (row >= 0 && row < artistlist[selected_artist].albums[selected_album].num_songs) {
                        selected_song = row;
                        int requested_song = artistlist[selected_artist].albums[selected_album].songs[selected_song].id;
                        printf("Selected song: %d\n", selected_song);
                        printf("Selected song name: %s\n", artistlist[selected_artist].albums[selected_album].songs[selected_song].title);
                        printf("Selected song id: %d\n", artistlist[selected_artist].albums[selected_album].songs[selected_song].id);

                        if (playback_running) {
                            paused = 0;
                            playback_running = 0;
                            pthread_cancel(playback_thread);
                            pthread_join(playback_thread, NULL);
                        }

                        struct PlaybackArgs *pa = malloc(sizeof(*pa));
                        pa->app = app;
                        pa->song_id = requested_song;

                        pthread_create(&playback_thread, NULL, playback_thread_func, pa);

                        break;
                    }
                }
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
                    XDrawString(display, app->artist_win, gc, 5, y + 20, artistlist[i].name, strlen(artistlist[i].name));
                }
            }

            if (ev.xbutton.window == app->album_win) {
                if (ev.xbutton.button == Button4) album_scroll -= ROW_HEIGHT;
                if (ev.xbutton.button == Button5) album_scroll += ROW_HEIGHT;

                XWindowAttributes attr;
                XGetWindowAttributes(display, app->album_win, &attr);
                int win_height = attr.height;
                int max_scroll = 0;
                if (selected_artist != -1) max_scroll = artistlist[selected_artist].num_albums * ROW_HEIGHT - win_height;
                if (max_scroll < 0) max_scroll = 0;
                if (album_scroll < 0) album_scroll = 0;
                if (album_scroll > max_scroll) album_scroll = max_scroll;

                XClearWindow(display, app->album_win);
                if (selected_artist != -1) {
                    for (int i = 0; i < artistlist[selected_artist].num_albums; i++) {
                        int y = i * ROW_HEIGHT - album_scroll;
                        if (y + ROW_HEIGHT < 0 || y > win_height) continue;
                        XDrawString(display, app->album_win, gc, 5, y + 20, artistlist[selected_artist].albums[i].title, strlen(artistlist[selected_artist].albums[i].title));
                    }
                }
            }

            if (ev.xbutton.window == app->song_win) {
                if (ev.xbutton.button == Button4) song_scroll -= ROW_HEIGHT;
                if (ev.xbutton.button == Button5) song_scroll += ROW_HEIGHT;

                XWindowAttributes attr;
                XGetWindowAttributes(display, app->song_win, &attr);
                int win_height = attr.height;
                int max_scroll = 0;
                if (selected_artist != -1 && selected_album != -1) max_scroll = artistlist[selected_artist].albums[selected_album].num_songs * ROW_HEIGHT - win_height;
                if (max_scroll < 0) max_scroll = 0;
                if (song_scroll < 0) song_scroll = 0;
                if (song_scroll > max_scroll) song_scroll = max_scroll;

                XClearWindow(display, app->song_win);
                if (selected_artist != -1 && selected_album != -1) {
                    for (int i = 0; i < artistlist[selected_artist].albums[selected_album].num_songs; i++) {
                        int y = i * ROW_HEIGHT - song_scroll;
                        if (y + ROW_HEIGHT < 0 || y > win_height) continue;
                        XDrawString(display, app->song_win, gc, 5, y + 20, artistlist[selected_artist].albums[selected_album].songs[i].title, strlen(artistlist[selected_artist].albums[selected_album].songs[i].title));
                    }
                }
            }
            break;

        case KeyPress: {
            KeySym keysym = XkbKeycodeToKeysym(
                display,
                ev.xkey.keycode,
                0,
                0
            );

            if (keysym == XK_space) {
                paused = !paused;
            }

            break;
        }

        case Expose:

            if (ev.xexpose.window == app->main_win) {
                XSetInputFocus(display, app->main_win, RevertToParent, CurrentTime);
            }
            
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
                    XDrawString(display, app->artist_win, gc, 5, y + 20, artistlist[i].name, strlen(artistlist[i].name));
                }
            }

            if (ev.xexpose.window == app->album_win) {
                XWindowAttributes attr;
                XGetWindowAttributes(display, app->album_win, &attr);
                int win_height = attr.height;

                XClearWindow(display, app->album_win);
                if (selected_artist != -1) {
                    for (int i = 0; i < artistlist[selected_artist].num_albums; i++) {
                        int y = i * ROW_HEIGHT - album_scroll;
                        if (y + ROW_HEIGHT < 0 || y > win_height) continue;
                        XDrawString(display, app->album_win, gc, 5, y + 20, artistlist[selected_artist].albums[i].title, strlen(artistlist[selected_artist].albums[i].title));
                    }
                }
            }

            if (ev.xexpose.window == app->song_win) {
                XWindowAttributes attr;
                XGetWindowAttributes(display, app->song_win, &attr);
                int win_height = attr.height;

                XClearWindow(display, app->song_win);
                if (selected_artist != -1 && selected_album != -1) {
                    for (int i = 0; i < artistlist[selected_artist].albums[selected_album].num_songs; i++) {
                        int y = i * ROW_HEIGHT - song_scroll;
                        if (y + ROW_HEIGHT < 0 || y > win_height) continue;
                        XDrawString(display, app->song_win, gc, 5, y + 20, artistlist[selected_artist].albums[selected_album].songs[i].title, strlen(artistlist[selected_artist].albums[selected_album].songs[i].title));
                    }
                }
            }
            break;
        }
    }
}



int main(int argc, char *argv[]) {
    Window win;
    XEvent ev;
    GC gc;

    struct App app = {0};
    app.server_host = xstrdup(argv[1]);
    app.start_arg = xstrdup(argv[2]);
    mpg123_init();

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
    app.musicButtons = create_win(POSX, HEIGHT-130, WIDTH, 120, 5, app.main_win, ExposureMask | KeyPressMask | ButtonPressMask | ButtonReleaseMask, WhitePixel(display, screen));
    app.pauseButton = create_win(WIDTH / 2, 44 / 2, 30, 30, 1, app.musicButtons, ExposureMask | KeyPressMask | ButtonPressMask | ButtonReleaseMask, WhitePixel(display, screen));
    Window nextSong = create_win(WIDTH / 2 + 50, 44 / 2, 30, 30, 1, app.musicButtons, ExposureMask, WhitePixel(display, screen));
    Window previousSong = create_win(WIDTH / 2 - 50, 44 / 2, 30, 30, 1, app.musicButtons, ExposureMask, WhitePixel(display, screen));
    //app.button   = create_win(0, 0, 250, 200, 10, app.main_win, ExposureMask, WhitePixel(display, screen));

    XMapWindow(display, app.main_win);
    XMapWindow(display, app.artist_win);
    XMapWindow(display, app.album_win);
    XMapWindow(display, app.song_win);
    XMapWindow(display, app.musicButtons);
    XMapWindow(display, app.pauseButton);
    XMapWindow(display, nextSong);
    XMapWindow(display, previousSong);

    gc = create_gc(LINE);

    connect_to_server(&app, argv[1], 1);
    //get_songlist(&app);
    receive_metadata(app.sock_fd, &app);

    run(&app, gc, app.num_artists);

    XUnmapWindow(display, app.main_win);
    XUnmapWindow(display, app.artist_win);
    XUnmapWindow(display, app.album_win);
    XUnmapWindow(display, app.song_win);
    XUnmapWindow(display, app.musicButtons);
    XUnmapWindow(display, app.pauseButton);
    XUnmapWindow(display, nextSong);
    XUnmapWindow(display, previousSong);

    XDestroyWindow(display, app.main_win);
    XDestroyWindow(display, app.artist_win);
    XDestroyWindow(display, app.album_win);
    XDestroyWindow(display, app.song_win);
    XDestroyWindow(display, app.musicButtons);
    XDestroyWindow(display, app.pauseButton);
    XDestroyWindow(display, nextSong);
    XDestroyWindow(display, previousSong);
    
    XCloseDisplay(display);

    close(app.sock_fd);
    
    return 0;
}