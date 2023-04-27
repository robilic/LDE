
LIBS = -lm

lde: lde.c
	gcc `pkg-config --cflags gtk4` -o lde lde.c `pkg-config --libs gtk4` $(LIBS)

