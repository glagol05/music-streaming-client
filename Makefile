CC = gcc
TARGET = xwindow
SRC = client.c
OBJ = client.o

CFLAGS = -Wall -Wextra -std=c11 -O2 \
		-D_POSIX_C_SOURCE=200112L \
		$(shell pkg-config --cflags x11 xft)

LDFLAGS = $(shell pkg-config --libs x11 xft)

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $(TARGET) $(LDFLAGS)

$(OBJ): $(SRC)
	$(CC) $(CFLAGS) -c $(SRC) -o $(OBJ)

clean:
	rm -f $(OBJ) $(TARGET)
