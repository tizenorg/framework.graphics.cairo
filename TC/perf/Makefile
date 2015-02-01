CC = gcc

all : fill stroke image

fill: main.c common.c fill.c
	$(CC) main.c common.c fill.c -g -o fill `pkg-config --cflags --libs cairo elementary opengl-es-20` -lecore_x -levas -lecore

stroke: main.c common.c stroke.c
	$(CC) main.c common.c stroke.c -g -o stroke `pkg-config --cflags --libs cairo elementary opengl-es-20` -lecore_x -levas -lecore

image: main.c common.c image.c
	$(CC) main.c common.c image.c -g -o image `pkg-config --cflags --libs cairo elementary opengl-es-20` -lecore_x -levas -lecore

clean:
	rm -rf *.0 fill stroke image
