#include <stdio.h>	/* for printf */
#include <stdint.h>	/* for uint64 definition */
#include <stdlib.h>	/* for exit() definition */
#include <time.h>	/* for clock_gettime */
#include <unistd.h> /* for sleep */
#include <math.h>

#include <gtk/gtk.h>

#include "lde_config.h"
#include "wadfile.h"

// For timing
#define BILLION 1000000000L

// In it's own header at some point?
#define EDIT_MODE_PAN      0
#define EDIT_MODE_THINGS   1
#define EDIT_MODE_VERTEXES 2
#define EDIT_MODE_LINEDEFS 3
#define EDIT_MODE_SIDEDEFS 4
#define EDIT_MODE_SECTORS  5

int vp_x = -1500, vp_y = -1000;     // upper left corner of the current viewport (in map coords)
int selectedObjectID = -1;
int editMode = EDIT_MODE_PAN;
double last_mouse_x, last_mouse_y;
double view_scale = 1;     // map scale, > 1 zoom out, < 1 zoom in -.10 to 8.0 should be the range
int grid_size = 100;        // grid size in map coords
int grid_shown = 1;

// DOOM LEVEL DATA
Thing *things;
LineDef *linedefs;
SideDef *sidedefs;
Vertex *vertexes;

Texture *textures;
Palette *palette;
Sprite *sprites;

unsigned char **sprite_images;
int TARGET_SPRITE;

SEG *segs;
SSector *ssectors;
Node *nodes;
Sector *sectors;

int things_count, linedefs_count, sidedefs_count, vertexes_count, sprites_count, textures_count;

// DOOM FUNCTIONS
// THIS NEEDS TO BE IN IT'S OWN FILE

int loadWadFile()
{
    FILE *wadFile;
    WADFileHeader header;
    WADFileDirectoryEntry *directory;
    int textures_pointer = 0;
    int sprites_pointer = 0;

    textures_count = 0;
    sprites_count = 0;

    wadFile = fopen(WADFILE_PATH, "r");
    
    if (wadFile == NULL) {
        printf(".WAD file not found :(\n");
        printf("errno = %d\n", errno);
        
        exit(0);
    }

    fread(&header, sizeof(WADFileHeader), 1, wadFile);
    printf("\nWAD file type: %c%c%c%c, dirsize: %d, dirstart: %d\n", header.type[0], header.type[1], header.type[2], header.type[3], header.dirsize, header.dirstart);
    printf("Directory entries: %d, total size: %lu\n", header.dirsize, header.dirsize * sizeof(WADFileDirectoryEntry));
    
    directory = malloc(header.dirsize * sizeof(WADFileDirectoryEntry));
    fseek(wadFile, header.dirstart, SEEK_SET);
    fread(directory, sizeof(WADFileDirectoryEntry), header.dirsize, wadFile);
    
    // find the desired level and load the data for it
    for (int i = 0; i < header.dirsize; i++) {
        if (!strncmp("E1M4", directory[i].name, 4)) {
            // printf("\nFound it! Entry #%d %d %d\n", i, directory[i].start, directory[i].size);
            // fread(void *restrict __ptr, size_t __size, size_t __nitems, FILE *restrict __stream)
            // printf("THINGS struct is %lu bytes each\n", sizeof(Thing));
            // printf("THINGS start at %d and consist of %d bytes\n", directory[i+1].start, directory[i+1].size);
            fseek(wadFile, directory[i+1].start, SEEK_SET);
            things = malloc(directory[i+1].size);
            things_count = directory[i+1].size / sizeof(Thing);
            fread(things, sizeof(Thing), directory[i+1].size / sizeof(Thing), wadFile);

            fseek(wadFile, directory[i+2].start, SEEK_SET);
            linedefs = malloc(directory[i+2].size);
            linedefs_count = directory[i+2].size / sizeof(LineDef);
            fread(linedefs, sizeof(LineDef), directory[i+2].size / sizeof(LineDef), wadFile);

            fseek(wadFile, directory[i+3].start, SEEK_SET);
            sidedefs = malloc(directory[i+3].size);
            sidedefs_count = directory[i+3].size / sizeof(SideDef);
            fread(sidedefs, sizeof(SideDef), directory[i+3].size / sizeof(SideDef), wadFile);

            fseek(wadFile, directory[i+4].start, SEEK_SET);
            vertexes = malloc(directory[i+4].size);
            vertexes_count = directory[i+4].size / sizeof(Vertex);
            fread(vertexes, sizeof(Vertex), directory[i+4].size / sizeof(Vertex), wadFile);
        }
        //printf("%.*s ", 8, directory[i].name);
    }
    printf("Loaded %d things, %d linedefs, %d sidedefs, %d vertexes\n", things_count, linedefs_count, sidedefs_count, vertexes_count);
    
    //
    // load color palette
    for (int i = 0; i < header.dirsize; i++) {
        if (!strncmp("PLAYPAL", directory[i].name, 7)) {
            //printf("Found PLAYPAL! Entry #%d %d %d\n", i, directory[i].start, directory[i].size);
            palette = malloc(sizeof(char) * 768); // we only need the first palette
            fseek(wadFile, directory[i].start, SEEK_SET);
            fread(palette, 768, 1, wadFile);
            break;
        }
    }
    /*
    printf("Palette dump:\n");
    for (int i = 0; i < 768; i += 3) {
        printf("%d: %x %x %x\n", i/3, palette[i/3].r, palette[i/3].g, palette[i/3].b);
    }
    */
    
    //
    // load floor and ceiling textures, count them first. find the start of textures
    for (int i = 0; i < header.dirsize; i++) {
        if (!strncmp("F1_START", directory[i].name, 7)) {
            //printf("\nFound it! Entry #%d %d %d\n", i, directory[i].start, directory[i].size);
            textures_pointer = i + 1;
            break;
        }
    }

    // go through the directory until we get to the end of the textures
    // TODO: why count these when we can check for this as we read them?
    for (int i = textures_pointer; i < header.dirsize; i++) {
        if (!strncmp("F1_END", directory[i].name, 6)) {
            break;
        }
        textures_count = i - textures_pointer;
    }

    // printf("Next entry is %.*s\n", 8, directory[textures_pointer].name);
    printf("Found %d floor/ceiling textures\n", textures_count);

    fseek(wadFile, directory[textures_pointer].start, SEEK_SET);
    textures = malloc(sizeof(Texture) * textures_count);
    for (int i = 0; i < textures_count; i++) {
        // copy the name from the directory to the textures structure
        memcpy(textures[i].name, directory[textures_pointer + i].name, 8);
        // now the image data
        fread(textures[i].data, 4096, 1, wadFile);
    }

    // load sprites
    for (int i = 0; i < header.dirsize; i++) {
        if (!strncmp("S_START", directory[i].name, 7)) {
            sprites_pointer = i + 1;
            break;
        }
    }
    for (int i = sprites_pointer; i < header.dirsize; i++) {
        if (!strncmp("S_END", directory[i].name, 5)) {
            break;
        }
        sprites_count = i - sprites_pointer;
    }
    printf("Sprites pointer is %d, %d sprites found!\n", sprites_pointer, sprites_count);
    // read all the sprite headers
    sprites = calloc(1, sizeof(Sprite) * sprites_count);
    for (int i = 0; i < sprites_count; i++) {
        fseek(wadFile, directory[sprites_pointer+i].start, SEEK_SET);
        fread(&sprites[i], sizeof(Sprite), 1, wadFile);
    }
    
    sprite_images = malloc(sizeof(unsigned char*) * sprites_count);
    int column_pointers[320];

    for (int i = 0; i < sprites_count; i++) {
            
        //    for (int i = 0; i < 100; i++) {
        //        printf("Sprite %d is %dx%d\n", i, sprites[i].width, sprites[i].height);
        //    }
            
            TARGET_SPRITE = i;
            //printf("Sprite %.*s is %dx%d found at %d. left offset = %d, top_offset = %d\n", 8, directory[sprites_pointer+TARGET_SPRITE].name, sprites[TARGET_SPRITE].width, sprites[TARGET_SPRITE].height, directory[sprites_pointer+TARGET_SPRITE].start, sprites[TARGET_SPRITE].left_offset, sprites[TARGET_SPRITE].top_offset);

            unsigned char *rotated_sprite; // the original sprite, the way doom stores it
            
            // array of pointers decoded image data for all the sprites
            // allocate memory for this particular sprite
            sprite_images[TARGET_SPRITE] = calloc(1, sizeof(unsigned char) * sprites[TARGET_SPRITE].width * sprites[TARGET_SPRITE].height);
            rotated_sprite = calloc(1, sizeof(unsigned char) * sprites[TARGET_SPRITE].width * sprites[TARGET_SPRITE].height);
            
            // sprite data header
            // 4 16-bit ints, w, h, left_offset, top_offset
            // width # of long pointers to rows of data
            // each row (bytes): row to start drawing
            //                   pixel count
            //                   blank byte, picture data bytes, blank
            //                   FF ends column (also will signify empty row)
            //                   other wise draw more data for this column
            
            // go to the start of the sprite data
            // sprites_pointer is the first/starting sprite
            fseek(wadFile, directory[sprites_pointer+TARGET_SPRITE].start + 8, SEEK_SET);
            int32_t image_offset; // where we are drawing in the current sprite
            unsigned char b1, b2;
            int sprite_data_start = directory[sprites_pointer+TARGET_SPRITE].start;
            int line_start;
            fread(&column_pointers, sizeof(int32_t), sprites[TARGET_SPRITE].width, wadFile);
            //printf("Sprite data starts at: %d %x\n", sprite_data_start, sprite_data_start);
            
            for (int i = 0; i < sprites[TARGET_SPRITE].width; i++) {
                //printf("Sprite column %d data lives at %d %x\n", i, column_pointers[i], column_pointers[i]);
                // read the next column every iteration of the loop
                fseek(wadFile, sprite_data_start + column_pointers[i], SEEK_SET);
                // start writing pixels at the beginning of the line
                line_start = sprites[TARGET_SPRITE].height * i;
                image_offset = line_start;

                while (TRUE) {
                    fread(&b1, sizeof(unsigned char), 1, wadFile); // row to start drawing
                    if (b1 == 0xff) {
                        //printf("Breaking on column %d\n", i);
                        break; // this whole line is blank/transparent
                    } else {
                        image_offset = line_start + b1;
                        //printf("image_offset = %d\n", image_offset);
                        fread(&b2, sizeof(unsigned char), 1, wadFile); // count of data bytes
                        fseek(wadFile, 1, SEEK_CUR); // skip first byte
                        fread(&rotated_sprite[image_offset], sizeof(unsigned char) * b2, 1, wadFile);
                        image_offset += b2;
                        fseek(wadFile, 1, SEEK_CUR); // skip last byte
                        //printf("Drawing column %d, starting at row %d\n", i, b1);
                        //printf("Copying %d bytes\n", b2);
                        if (image_offset > sprites[TARGET_SPRITE].height * (i + 1)) {
                            printf("Image offset is off the grid: offset %d, eol: %d\n", image_offset, sprites[TARGET_SPRITE].height * (i + 1));
                        }
                    }
                    if (image_offset > sprites[TARGET_SPRITE].width * sprites[TARGET_SPRITE].height) {
                        printf("Something went wrong, image data is larger than sprite dimensions\n");
                        exit(1);
                    }
                }
            }
            // dump sprite to text
            //    for (int i = 0; i < sprites[0].height * sprites[0].width; i++) {
            //        printf("%x", sprite_images[0][i]);
            //    }

            // flip the sprite, because doom stores them rotated 90 degrees to the left
            for (int x = 0; x < sprites[TARGET_SPRITE].height; x++) {
                    for (int y = 0; y < sprites[TARGET_SPRITE].width; y++) {
                    sprite_images[TARGET_SPRITE][x + (y * sprites[TARGET_SPRITE].height)] =
                                  rotated_sprite[x + (y * sprites[TARGET_SPRITE].height)];
                }
            }
            // don't forget to swap height/width on the sprite data structure
            /*int swap = sprites[TARGET_SPRITE].height;
            sprites[TARGET_SPRITE].height = sprites[TARGET_SPRITE].width;
            sprites[TARGET_SPRITE].width = swap;
            */
        free(rotated_sprite);
    }

    printf("Closing .WAD file\n");
    fclose(wadFile);
    
    /* dump sidedefs
     
    for (int v = 0; v < sidedefs_count; v++) {
        printf("xoff: %d yoff: %d %.*s %.*s %.*s %d\n", sidedefs[v].xoff, sidedefs[v].yoff, 8, sidedefs[v].tex1, 8, sidedefs[v].tex2, 8, sidedefs[v].tex3, sidedefs[v].sector);
    }
    printf("\nWrote %d sidedefs\n", sidedefs_count);
    */
    
    /*
      How to access the first, and the last items in our directory
    
    printf("\n%.*s \n\n %d %d\n", 16, directory->name, directory->start, directory->size);
    printf("\n%.*s \n\n %d %d\n", 16, directory[1263].name, directory[1263].start, directory[1263].size);
     
    */
    
// dump WAD file directory
//    for (int i = 0; i < header.dirsize; i++) {
//        fread(&Entry, sizeof(Entry), 1, WADFile);
//        printf("%.*s %d %d\n", 8, Entry.name, Entry.size, Entry.start);
//    }

    printf("applicationDidFinishLaunching\n");
}

#pragma Utilities

double pointDistance(int x1, int y1, int x2, int y2)
{
    // calculate the hypotenuse to find the distance
    return sqrt(pow(x2 - x1, 2) + pow(y2 - y1, 2));
}

int hitDetectLine(int line, int testx, int testy)
{
    double buffer = 3; // how close we must be to being on in the line
    double d1 = pointDistance(testx, testy, vertexes[linedefs[line].end].x, vertexes[linedefs[line].end].y);
    double d2 = pointDistance(testx, testy, vertexes[linedefs[line].start].x, vertexes[linedefs[line].start].y);
    double ll = pointDistance(vertexes[linedefs[line].end].x, vertexes[linedefs[line].end].y, vertexes[linedefs[line].start].x, vertexes[linedefs[line].start].y);
    
    if (d1 + d2 >= ll - buffer && d1 + d2 <= ll + buffer) {
        return true;
    } else {
        return false;
    }
}

int is_point_in_view(int x_size, int y_size, int point_x, int point_y) {
  if (point_x >= 0 & point_x <= x_size)
  {
    if (point_y >= 0 && point_y <= x_size)
    {
      return 1;
    }
  }
  return 0;
}


// ***************

/* Surface to store current scribbles */
static cairo_surface_t *surface = NULL;

static void
clear_surface (void)
{
  cairo_t *cr;

  cr = cairo_create (surface);

  cairo_set_source_rgb (cr, 0.1, 0.1, 0.1);
  cairo_paint (cr);

  cairo_destroy (cr);
}

/* Create a new surface of the appropriate size to store our scribbles */
static void
resize_cb (GtkWidget *widget,
           int        width,
           int        height,
           gpointer   data)
{
  if (surface)
    {
      cairo_surface_destroy (surface);
      surface = NULL;
    }

  if (gtk_native_get_surface (gtk_widget_get_native (widget)))
    {
      surface = gdk_surface_create_similar_surface (gtk_native_get_surface (gtk_widget_get_native (widget)),
                                                    CAIRO_CONTENT_COLOR,
                                                    gtk_widget_get_width (widget),
                                                    gtk_widget_get_height (widget));

      /* Initialize the surface to white */
      clear_surface ();
    }
}

/* Redraw the screen from the surface. Note that the draw
 * callback receives a ready-to-be-used cairo_t that is already
 * clipped to only draw the exposed areas of the widget
 */
static void
draw_cb (GtkDrawingArea *drawing_area,
         cairo_t        *cr,
         int             width,
         int             height,
         gpointer        data)
{
  cairo_set_source_surface (cr, surface, 0, 0);
  cairo_paint (cr);
}

/* map coordinates to viewport-relative coordinates */
#define SCREENX(x) ((x - vp_x)/view_scale)
#define SCREENY(y) ((y - vp_y)/view_scale)

/* Draw a rectangle on the surface at the given position */
static void
draw_viewport (GtkWidget *widget)
{
  uint64_t diff;
  struct timespec start, end;
  int viewport_size_x = gtk_widget_get_width(widget);
  int viewport_size_y = gtk_widget_get_height(widget);

  //printf("Area is %d by %d\n", gtk_widget_get_width(widget), gtk_widget_get_height(widget));

  /* measure monotonic time */
  clock_gettime(CLOCK_MONOTONIC, &start); /* mark start time */

  cairo_t *cr;
  float linedef_color;

  /* Paint to the surface, where we store our state */
  cr = cairo_create (surface);
  cairo_set_source_rgb(cr, 0, 0, 0);
  cairo_paint(cr);

  //cairo_set_source_rgb(cr,0.8, 0.0, 0.0);
  cairo_set_line_width(cr,1.0);

  for (int i=0; i<linedefs_count; i++) {
    if ( is_point_in_view(viewport_size_x, viewport_size_y, SCREENX(vertexes[linedefs[i].start].x), SCREENY(vertexes[linedefs[i].start].y)) ||
         is_point_in_view(viewport_size_x, viewport_size_y, SCREENX(vertexes[linedefs[i].end].x), SCREENY(vertexes[linedefs[i].end].y)))
    {
      cairo_move_to(cr, SCREENX(vertexes[linedefs[i].start].x), SCREENY(vertexes[linedefs[i].start].y));
      linedef_color = 50 + (sidedefs[linedefs[i].sidedef1].sector * 2);
      cairo_set_source_rgb(cr,0.8, linedef_color/225, linedef_color/256);
      cairo_line_to(cr, SCREENX(vertexes[linedefs[i].end].x), SCREENY(vertexes[linedefs[i].end].y));
      cairo_stroke(cr);
    }
  }
  // draw vertexes - they are all gray
  cairo_set_source_rgb(cr, 0.6, 0.6, 0.6);
  for (int v=0; v<vertexes_count; v++) {
    if ( is_point_in_view(viewport_size_x, viewport_size_y, SCREENX(vertexes[v].x), SCREENY(vertexes[v].y)) ) {
      cairo_rectangle(cr, SCREENX(vertexes[v].x)-1, SCREENY(vertexes[v].y)-1, 2, 2);
    }
  }
  cairo_stroke(cr);

  // draw things
  cairo_set_source_rgb(cr, 1, 1, 0.3);
  for (int t=0; t<things_count; t++) {
    if ( is_point_in_view(viewport_size_x, viewport_size_y, SCREENX(things[t].x), SCREENY(things[t].y)) ) {
      cairo_arc(cr, SCREENX(things[t].x), SCREENY(things[t].y), 3, 0.0, 2 * M_PI);
      cairo_stroke(cr);
    }
  }

  // draw highlighted entity
  // if MODE == "foo" ...
  
  if (selectedObjectID > -1) {
      switch (editMode) {
          case EDIT_MODE_THINGS:
            cairo_set_source_rgb(cr, 0, 0.96, 1.0); // cyan
            cairo_rectangle(cr, ((things[selectedObjectID].x-vp_x)/view_scale)-5, ((things[selectedObjectID].y-vp_y)/view_scale)-5, 10, 10);
            cairo_stroke(cr);
            break;
          case EDIT_MODE_VERTEXES:
            cairo_set_source_rgb(cr, 0, 0.96, 1.0); // cyan
            cairo_rectangle(cr, ((vertexes[selectedObjectID].x-vp_x)/view_scale)-3, ((vertexes[selectedObjectID].y-vp_y)/view_scale)-3, 6, 6);
            cairo_stroke(cr);
            break;
          case EDIT_MODE_LINEDEFS:
            cairo_set_source_rgb(cr, 0, 1.0, 1.0); // cyan
            cairo_move_to(cr, (vertexes[linedefs[selectedObjectID].start].x-vp_x) / view_scale, (vertexes[linedefs[selectedObjectID].start].y-vp_y) / view_scale);
            cairo_line_to(cr, (vertexes[linedefs[selectedObjectID].end].x-vp_x) / view_scale, (vertexes[linedefs[selectedObjectID].end].y-vp_y) / view_scale);
            cairo_stroke(cr);
            break;
          default:
            break;
      }
  }

  cairo_destroy (cr);

  /* Now invalidate the drawing area. */
  gtk_widget_queue_draw (widget);

  clock_gettime(CLOCK_MONOTONIC, &end);   /* mark the end time */
  diff = BILLION * (end.tv_sec - start.tv_sec) + end.tv_nsec - start.tv_nsec;
  printf("redraw time = %llu nanoseconds\n", (long long unsigned int)diff);
}

static void
drag_begin (GtkGestureDrag *gesture,
            double          x,
            double          y,
            GtkWidget      *area)
{
  last_mouse_x = 0;
  last_mouse_y = 0;
  draw_viewport(area);
//  printf("drag_begin: %f, %f\n", x, y);
}

static void
drag_update (GtkGestureDrag *gesture,
             double          x,
             double          y,
             GtkWidget      *area)
{
  double piv_x, piv_y;
  piv_x = x;
  piv_y = y;
  
  printf("%f  X, Y %f, %f", view_scale, x, y);
  printf("  calced: %f, %f   %d, %d\n", (view_scale*piv_x), (view_scale*piv_y), (int)(view_scale*piv_x), (int)(view_scale*piv_y));

  switch (editMode) {
    case EDIT_MODE_PAN:
      vp_x = round(vp_x + (view_scale*(last_mouse_x - x)));
      vp_y = round(vp_y + (view_scale*(last_mouse_y - y)));
      break;
    case EDIT_MODE_THINGS:
      if (selectedObjectID > -1) {
        things[selectedObjectID].x -= round(view_scale*(last_mouse_x - x));
        things[selectedObjectID].y -= round(view_scale*(last_mouse_y - y));
      }
      break;
    default:
      break;
  }
  last_mouse_x = x;
  last_mouse_y = y;
  draw_viewport(area);
}

static void
drag_end (GtkGestureDrag *gesture,
          double          x,
          double          y,
          GtkWidget      *area)
{
  last_mouse_x = 0;
  last_mouse_y = 0;
  draw_viewport(area);
  printf("drag_end: %f, %f\n", x, y);
  printf("Viewport is at %d, %d\n", vp_x, vp_y);
}

static void
pressed (GtkGestureClick *gesture,
         int              n_press,
         double           x,
         double           y,
         GtkWidget       *area)
{
  //  clear_surface ();
  draw_viewport(area);
  //  gtk_widget_queue_draw (area);
}

static void
enter (
  GtkEventControllerMotion* self,
  gdouble x,
  gdouble y,
  gpointer user_data)
{
  //printf("motion_enter: %f, %f\n", x, y);
}

static void
leave (
  GtkEventControllerMotion* self,
  gpointer user_data)
{
  //printf("motion_leave\n");
}

static void
motion_cb (
  GtkEventControllerMotion* self,
  gdouble x,
  gdouble y,
  gpointer user_data)
{
  static int lastXpos, lastYpos;
  int curXpos, curYpos;
  double piv_x, piv_y;
  piv_x = x;
  piv_y = y;
  // we have WINDOW coords and we have LEVEL coords....
  
  curXpos = round(piv_x);
  curYpos = round(piv_y);

  if ((curXpos == lastXpos) && (curYpos == lastYpos)) {
      // mouse didn't move far enough to matter
  } else {
      lastXpos = curXpos;
      lastYpos = curYpos;
  }
  // calc level coords
  // move these to structs....
  int cursorLevelPosX = vp_x + (curXpos * view_scale);
  int cursorLevelPosY = vp_y + (curYpos * view_scale);
  
  int thing_hit_radius = 5 * view_scale;
  int vertex_hit_radius = 3 * view_scale;  
  int redraw = 0;

  switch (editMode) {
    case EDIT_MODE_PAN:
  //            printf("Mouse is at coords %d, %d\n", curXpos, curYpos);
        break;
    case EDIT_MODE_THINGS:
        for (int i = 0; i < things_count; i++) {
            if ((cursorLevelPosX > things[i].x - thing_hit_radius) && (cursorLevelPosX < things[i].x + thing_hit_radius)) {
                if ((cursorLevelPosY > things[i].y - thing_hit_radius) && (cursorLevelPosY < things[i].y + thing_hit_radius)) {
                    printf("Mouse hit on thing %d, type = %x (%d), attributes = %x\n", i, things[i].type, things[i].type, things[i].when);
                    selectedObjectID = i;
                    redraw = 1;
                }
            }
        }
        break;
        case EDIT_MODE_VERTEXES:
            for (int i = 0; i < vertexes_count; i++) {
                if ((cursorLevelPosX > vertexes[i].x - vertex_hit_radius) && (cursorLevelPosX < vertexes[i].x + vertex_hit_radius)) {
                    if ((cursorLevelPosY > vertexes[i].y - vertex_hit_radius) && (cursorLevelPosY < vertexes[i].y + vertex_hit_radius)) {
                        //printf("Mouse hit on thing %d, x:%d, y:%d\n", i, vertexes[i].x, vertexes[i].y);
                        selectedObjectID = i;
                        redraw = 1;
                    }
                }
            }
            break;
        case EDIT_MODE_LINEDEFS: {
            for (int i = 0; i < linedefs_count; i++) {
                if (hitDetectLine(i, cursorLevelPosX, cursorLevelPosY)) {
                    //printf("Hit on line %d\n", i);
                    selectedObjectID = i;
                    redraw = 1;
                }
            }
        }
        break;

      default:
          break;
  }
  if (redraw) {
    // redraw the view in case another object is hovered over
    GtkWidget *area = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(self));
    draw_viewport(area);
  }
}

// Key Input

static gboolean
key_pressed (GtkEventControllerKey *event_controller,
                      guint                  keyval,
                      guint                  keycode,
                      GdkModifierType        state,
                      gpointer user_data)
{
  printf("key_pressed\n");

  if (state & (GDK_SHIFT_MASK | GDK_CONTROL_MASK | GDK_ALT_MASK))
      return FALSE;

  guint key_val = keyval;
//  printf("key_pressed %d\n", key_val);
  switch (key_val)
  {
  case GDK_KEY_equal:
    view_scale -= 0.25;
    break;
  case GDK_KEY_minus:
    view_scale += 0.25;
    break;
  case GDK_KEY_v:
    editMode = EDIT_MODE_VERTEXES;
    break;
  case GDK_KEY_l:
    editMode = EDIT_MODE_LINEDEFS;
    break;
  case GDK_KEY_t:
    editMode = EDIT_MODE_THINGS;
    break;
  
  default:
    break;
  }
  printf("view_scale = %f\n", view_scale);

  GtkWidget *area = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(event_controller));
  draw_viewport(area);
  return TRUE;
}

static gboolean
key_released (GtkEventControllerKey *event_controller,
                      guint                  keyval,
                      guint                  keycode,
                      GdkModifierType        state,
                      gpointer user_data)
{
  printf("key_released\n");
  return FALSE;
}

static void
close_window (void)
{
  if (surface)
    cairo_surface_destroy (surface);
}

static void
activate (GtkApplication *app,
          gpointer        user_data)
{
  GtkWidget *window;
  GtkWidget *frame;
  GtkWidget *drawing_area;
  GtkWidget *scroller;
  
  GtkGesture *drag;
  GtkGesture *press;
  GtkEventController *motion, *key_controller;

  window = gtk_application_window_new (app);
  gtk_window_set_title (GTK_WINDOW (window), "Linux Doom(tm) Editor");

  g_signal_connect (window, "destroy", G_CALLBACK (close_window), NULL);

  frame = gtk_frame_new (NULL);
  gtk_window_set_child (GTK_WINDOW (window), frame);

  drawing_area = gtk_drawing_area_new ();
  /* set a minimum size */
  gtk_widget_set_size_request (drawing_area, 800, 800);
  gtk_widget_set_size_request (frame, 800, 800);
  gtk_frame_set_child (GTK_FRAME (frame), drawing_area);
 
//  scroller = gtk_scrolled_window_new();
//  gtk_frame_set_child (GTK_FRAME (frame), scroller);
//  gtk_scrolled_window_set_child((GtkScrolledWindow*)scroller, drawing_area);

  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (drawing_area), draw_cb, NULL, NULL);

  g_signal_connect_after (drawing_area, "resize", G_CALLBACK (resize_cb), NULL);

  drag = gtk_gesture_drag_new ();
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (drag), GDK_BUTTON_PRIMARY);
  gtk_widget_add_controller (drawing_area, GTK_EVENT_CONTROLLER (drag));
  g_signal_connect (drag, "drag-begin", G_CALLBACK (drag_begin), drawing_area);
  g_signal_connect (drag, "drag-update", G_CALLBACK (drag_update), drawing_area);
  g_signal_connect (drag, "drag-end", G_CALLBACK (drag_end), drawing_area);

  press = gtk_gesture_click_new ();
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (press), GDK_BUTTON_SECONDARY);
  gtk_widget_add_controller (drawing_area, GTK_EVENT_CONTROLLER (press));
  g_signal_connect (press, "pressed", G_CALLBACK (pressed), drawing_area);

  key_controller = gtk_event_controller_key_new ();
  gtk_widget_add_controller (drawing_area, GTK_EVENT_CONTROLLER (key_controller));
  g_signal_connect (key_controller, "key-pressed", G_CALLBACK (key_pressed), drawing_area);
  g_signal_connect (key_controller, "key-released", G_CALLBACK (key_released), drawing_area);
  gtk_widget_set_focusable(drawing_area, TRUE);

  // motion, "enter", "leave", signals
  // "is-pointer contains-pointer" are properties
  motion = gtk_event_controller_motion_new();
  gtk_widget_add_controller(drawing_area, GTK_EVENT_CONTROLLER(motion));
  //motion = motion_controller_for (GTK_WIDGET (drawing_area));
  g_signal_connect (motion, "motion", G_CALLBACK (motion_cb), drawing_area);
  g_signal_connect (motion, "leave", G_CALLBACK (leave), drawing_area);
  g_signal_connect (motion, "enter", G_CALLBACK (enter), drawing_area);

  gtk_widget_set_visible (window, true);
  loadWadFile();
}

int
main (int    argc,
      char **argv)
{
  GtkApplication *app;
  int status;

  app = gtk_application_new ("org.gtk.example", G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect (app, "activate", G_CALLBACK (activate), NULL);
  status = g_application_run (G_APPLICATION (app), argc, argv);
  g_object_unref (app);

  return status;
}
