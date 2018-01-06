/* Display an image with gtk3 and libvips. 
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>

#include <vips/vips.h>

#include "colorspaces.h"

#include "disp.h"




//#define USE_PYRAMID
#define USE_TILECACHE0
//#define USE_TILECACHE
#define CACHE_TS 64

G_DEFINE_TYPE( Imagedisplay, imagedisplay, GTK_TYPE_DRAWING_AREA );

/* Our signals. 
 */
enum {
	/* Our signals. 
	 */
	SIG_PRELOAD,
	SIG_LOAD,
	SIG_POSTLOAD,

	SIG_LAST
};

static guint imagedisplay_signals[SIG_LAST] = { 0 };

static void
free_mem_array (VipsObject *object, gpointer user_data)
{
  if( user_data ) free( user_data );
}


static int
vips_copy_metadata( VipsImage* in, VipsImage* out )
{
  if( !out ) return 0;
  int Xsize = out->Xsize;
  int Ysize = out->Ysize;
  int bands = out->Bands;
  VipsBandFormat fmt = out->BandFmt;
  VipsCoding coding = out->Coding;
  VipsInterpretation type = out->Type;
  gdouble xres = out->Xres;
  gdouble yres = out->Yres;
  VipsImage* invec[2] = {in, NULL};
  vips__image_copy_fields_array( out, invec );
  vips_image_init_fields( out,
      Xsize, Ysize, bands, fmt,
      coding, type, xres, yres
      );
return 0;
}


static void
imagedisplay_empty( Imagedisplay *imagedisplay )
{
	if( imagedisplay->image ) { 
		if( imagedisplay->preeval_sig ) { 
			g_signal_handler_disconnect( imagedisplay->image, 
				imagedisplay->preeval_sig ); 
			imagedisplay->preeval_sig = 0;
		}

		if( imagedisplay->eval_sig ) { 
			g_signal_handler_disconnect( imagedisplay->image, 
				imagedisplay->eval_sig ); 
			imagedisplay->eval_sig = 0;
		}

		if( imagedisplay->posteval_sig ) { 
			g_signal_handler_disconnect( imagedisplay->image, 
				imagedisplay->posteval_sig ); 
			imagedisplay->posteval_sig = 0;
		}
	}

	VIPS_UNREF( imagedisplay->srgb_region );
	VIPS_UNREF( imagedisplay->srgb );
	VIPS_UNREF( imagedisplay->mask_region );
	VIPS_UNREF( imagedisplay->mask );
	VIPS_UNREF( imagedisplay->display_region );
	VIPS_UNREF( imagedisplay->display );
	VIPS_UNREF( imagedisplay->image_region );

	VIPS_UNREF( imagedisplay->image );
}

static void
imagedisplay_destroy( GtkWidget *widget )
{
	Imagedisplay *imagedisplay = (Imagedisplay *) widget;

	printf( "imagedisplay_destroy:\n" ); 

	imagedisplay_empty( imagedisplay );

	GTK_WIDGET_CLASS( imagedisplay_parent_class )->destroy( widget );
}

static void
imagedisplay_draw_rect( Imagedisplay *imagedisplay, 
	cairo_t *cr, VipsRect *expose )
{
	VipsRect image;
	VipsRect clip;
	gboolean found;
	VipsPel * restrict buf;
	int lsk;
	int x, y;
	unsigned char *cairo_buffer;
	cairo_surface_t *surface;

	VipsRect expose_s;
  expose_s.top = expose->top * imagedisplay->device_scale;
  expose_s.left = expose->left * imagedisplay->device_scale;
  expose_s.width = expose->width * imagedisplay->device_scale;
  expose_s.height = expose->height * imagedisplay->device_scale;

	/*
	printf( "imagedisplay_draw_rect: "
		"left = %d, top = %d, width = %d, height = %d\n",
		expose->left, expose->top,
		expose->width, expose->height );
  */

	/* Clip against the image size ... we don't want to try painting 
	 * outside the image area.
	 */
	image.left = 0;
	image.top = 0;
	image.width = imagedisplay->srgb_region->im->Xsize;
	image.height = imagedisplay->srgb_region->im->Ysize;
	vips_rect_intersectrect( &image, &expose_s, &clip );
	if( vips_rect_isempty( &clip ) )
		return;

	g_assert( imagedisplay->srgb_region->im == imagedisplay->srgb ); 
	g_assert( imagedisplay->mask_region->im == imagedisplay->mask ); 

  /*
  printf( "                  clip: "
    "left = %d, top = %d, width = %d, height = %d\n",
    clip.left, clip.top,
    clip.width, clip.height );
  */

  /* Request pixels. We ask the mask first, to get an idea of what's
	 * currently in cache, then request tiles of pixels. We must always
	 * request pixels, even if the mask is blank, because the request
	 * will trigger a notify later which will reinvoke us.
	 */
	if( vips_region_prepare( imagedisplay->mask_region, &clip ) ) {
		printf( "vips_region_prepare: %s\n", vips_error_buffer() ); 
		vips_error_clear();
		abort();
		return;
	}

	if( vips_region_prepare( imagedisplay->srgb_region, &clip ) ) {
		printf( "vips_region_prepare: %s\n", vips_error_buffer() ); 
		vips_error_clear();
		abort();
		return;
	}

	/* If the mask is all blank, skip the paint.
	 */
	buf = VIPS_REGION_ADDR( imagedisplay->mask_region, 
		clip.left, clip.top );
	lsk = VIPS_REGION_LSKIP( imagedisplay->mask_region );
	found = FALSE;

	for( y = 0; y < clip.height; y++ ) {
		for( x = 0; x < clip.width; x++ )
			if( buf[x] ) {
				found = TRUE;
				break;
			}

		if( found )
			break;

		buf += lsk;
	}

	if( !found ) {
		printf( "imagedisplay_paint_image: zero mask\n" );
		return;
	}

	/*
	printf( "imagedisplay_paint_image: painting %d x %d pixels\n", 
		clip.width, clip.height );
  */
	/* libvips is RGB, cairo is ARGB, we have to repack the data.
	 */
	cairo_buffer = g_malloc( clip.width * clip.height * 4 );

	for( y = 0; y < clip.height; y++ ) {
		VipsPel * restrict p = 
			VIPS_REGION_ADDR( imagedisplay->srgb_region, 
			clip.left, clip.top + y );
		unsigned char * restrict q = cairo_buffer + clip.width * 4 * y;

		for( x = 0; x < clip.width; x++ ) {
			q[0] = p[2];
			q[1] = p[1];
			q[2] = p[0];
			q[3] = 0;

			p += 3;
			q += 4;
		}
	}

	surface = cairo_image_surface_create_for_data( cairo_buffer, 
		CAIRO_FORMAT_RGB24, clip.width, clip.height, clip.width * 4 );

	cairo_surface_set_device_scale(surface, imagedisplay->device_scale, imagedisplay->device_scale);
  double x_scale, y_scale;
  cairo_surface_get_device_scale(surface, &x_scale, &y_scale);
  /*printf("device scale: %f %f\n", (float)x_scale, (float)y_scale);*/

  cairo_set_source_surface( cr, surface,
      clip.left / imagedisplay->device_scale,
      clip.top / imagedisplay->device_scale );

	cairo_paint( cr );



  g_free( cairo_buffer );

	cairo_surface_destroy( surface ); 
}

static gboolean
imagedisplay_draw( GtkWidget *widget, cairo_t *cr )
{
	Imagedisplay *imagedisplay = (Imagedisplay *) widget;

	/*printf( "imagedisplay_draw:\n" );*/

	if( imagedisplay->srgb_region ) {
		cairo_rectangle_list_t *rectangle_list = 
			cairo_copy_clip_rectangle_list( cr );

		if( rectangle_list->status == CAIRO_STATUS_SUCCESS ) { 
			int i;

			for( i = 0; i < rectangle_list->num_rectangles; i++ ) {
				cairo_rectangle_t *rectangle = 
					&rectangle_list->rectangles[i];
				VipsRect expose;

				expose.left = rectangle->x;
				expose.top = rectangle->y;
				expose.width = rectangle->width;
				expose.height = rectangle->height;

				imagedisplay_draw_rect( imagedisplay, 
					cr, &expose );
			}
		}

		cairo_rectangle_list_destroy( rectangle_list );
	}

	return( FALSE ); 
}

static void
imagedisplay_init( Imagedisplay *imagedisplay )
{
	printf( "imagedisplay_init:\n" ); 

  imagedisplay->pyramid = NULL;
  imagedisplay->mag = 1;
  imagedisplay->device_scale = 1;
}

static void
imagedisplay_class_init( ImagedisplayClass *class )
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS( class );

	printf( "imagedisplay_class_init:\n" ); 

	widget_class->destroy = imagedisplay_destroy;
	widget_class->draw = imagedisplay_draw;

	imagedisplay_signals[SIG_PRELOAD] = g_signal_new( "preload",
		G_TYPE_FROM_CLASS( class ),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET( ImagedisplayClass, preload ), 
		NULL, NULL,
		g_cclosure_marshal_VOID__POINTER,
		G_TYPE_NONE, 1,
		G_TYPE_POINTER );

	imagedisplay_signals[SIG_LOAD] = g_signal_new( "load",
		G_TYPE_FROM_CLASS( class ),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET( ImagedisplayClass, load ), 
		NULL, NULL,
		g_cclosure_marshal_VOID__POINTER,
		G_TYPE_NONE, 1,
		G_TYPE_POINTER );

	imagedisplay_signals[SIG_POSTLOAD] = g_signal_new( "postload",
		G_TYPE_FROM_CLASS( class ),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET( ImagedisplayClass, postload ), 
		NULL, NULL,
		g_cclosure_marshal_VOID__POINTER,
		G_TYPE_NONE, 1,
		G_TYPE_POINTER );

}

static void
imagedisplay_close_memory( VipsImage *image, gpointer contents )
{
	g_free( contents );
}

typedef struct _ImagedisplayUpdate {
	Imagedisplay *imagedisplay;
	VipsImage *image;
	VipsRect rect;
} ImagedisplayUpdate;

/* The main GUI thread runs this when it's idle and there are tiles that need
 * painting. 
 */
static gboolean
imagedisplay_render_cb( ImagedisplayUpdate *update )
{
	Imagedisplay *imagedisplay = update->imagedisplay;

	/* Again, stuff can run here long after the image has vanished, check
	 * before we update.
	 */

  /*
  printf( "imagedisplay_render_cb: "
    "left = %d, top = %d, width = %d, height = %d\n",
    update->rect.left, update->rect.top,
    update->rect.width, update->rect.height );
  */

	if( update->image == imagedisplay->srgb )  
		gtk_widget_queue_draw_area( GTK_WIDGET( update->imagedisplay ),
			update->rect.left / imagedisplay->device_scale,
			update->rect.top / imagedisplay->device_scale,
			update->rect.width / imagedisplay->device_scale,
			update->rect.height / imagedisplay->device_scale );

	g_free( update );

	return( FALSE );
}

/* Come here from the vips_sink_screen() background thread when a tile has been
 * calculated. We can't paint the screen directly since the main GUI thread
 * might be doing something. Instead, we add an idle callback which will be
 * run by the main GUI thread when it next hits the mainloop.
 */
static void
imagedisplay_render_notify( VipsImage *image, VipsRect *rect, void *client )
{
	Imagedisplay *imagedisplay = (Imagedisplay *) client;

  /*
  printf( "imagedisplay_render_notify: "
    "left = %d, top = %d, width = %d, height = %d\n",
    rect->left, rect->top,
    rect->width, rect->height );
  */

	/* We can come here after imagedisplay has junked this image and
	 * started displaying another. Check the image is still correct.
	 */
	if( image == imagedisplay->srgb ) { 
		ImagedisplayUpdate *update = g_new( ImagedisplayUpdate, 1 );

		update->imagedisplay = imagedisplay;
		update->image = image;
		update->rect = *rect;

		g_idle_add( (GSourceFunc) imagedisplay_render_cb, update );
	}
}


static int
get_pyramid_level( Imagedisplay *imagedisplay, float* mag )
{
  int level = 0, mag2 = 1, i;
  printf("get_pyramid_level: mag=%f\n", -imagedisplay->mag);
  while(mag2*2 < -imagedisplay->mag) {
    level += 1;
    mag2 *= 2;
    printf("get_pyramid_level: level=%d mag2=%d\n", level, mag2);
  }
  printf("get_pyramid_level: mag=%f level=%d\n", -imagedisplay->mag, level);
  mag2 = 1;
  for(i = 0; i <= level; i++) {
    if( !(imagedisplay->pyramid[i]) ) break;
    if( i>0 ) mag2 *= 2;
  }
  if( mag ) *mag = mag2;
  return(i-1);
}


/* Make the screen image. This is the thing we display pixel values from in
 * the status bar.
 */
static VipsImage *
imagedisplay_display_image( Imagedisplay *imagedisplay, VipsImage *in )
{
	VipsImage *image;
	VipsImage *x;

	printf("imagedisplay_display_image: mag=%f\n", imagedisplay->mag);

	/* image redisplays the head of the pipeline. Hold a ref to it as we
	 * work.
	 */
	image = in;
#ifndef USE_PYRAMID
	g_object_ref( image );
#endif

	if( imagedisplay->mag < 0 ) {
#ifndef USE_PYRAMID
	  if( vips_subsample( image, &x,
	      (int)-imagedisplay->mag, (int)-imagedisplay->mag, NULL ) ) {
	    g_object_unref( image );
	    return( NULL );
	  }
#else
	  float mag2;
	  int level = get_pyramid_level( imagedisplay, &mag2 );
	  if(level < 0) {
	    return NULL;
	  }
    image = imagedisplay->pyramid[level];
	  g_object_ref( image );
    printf("imagedisplay_display_image: level=%d mag2=%f\n", level, mag2);

	  float resizefac = -mag2 / imagedisplay->mag;
    printf("imagedisplay_display_image: pyrmid level: %dx%d resizefac=%f\n",
        image->Xsize, image->Ysize, resizefac);

		if( vips_resize( image, &x, resizefac, "kernel", VIPS_KERNEL_CUBIC, NULL ) ) {
			g_object_unref( image );
			return( NULL ); 
		}
		g_object_unref( image );
#endif
		image = x;
	}
	else { 
#ifdef USE_PYRAMID
	  g_object_ref( image );
#endif
		if( vips_zoom( image, &x, 
			imagedisplay->mag, imagedisplay->mag, NULL ) ) {
			g_object_unref( image );
			return( NULL ); 
		}
		g_object_unref( image );
		image = x;
	}

	return( image );
}

/* Make the srgb image we paint with. 
 */
static VipsImage *
imagedisplay_srgb_image( Imagedisplay *imagedisplay, VipsImage *in, 
	VipsImage **mask )
{
	VipsImage *image;
	VipsImage *x;

	/* image redisplays the head of the pipeline. Hold a ref to it as we
	 * work.
	 */
	image = in;
	g_object_ref( image ); 

	/*
	if( vips_icc_transform_float( image, &x, sRGBProfile(), NULL ) ) {
		g_object_unref( image );
	  printf("vips_icc_transform failed.\n");
		return( NULL ); 
	}
  printf("vips_icc_transform: image=%p x=%p\n", image, x);
  VIPS_UNREF( image );
	image = x;
	*/

	/* Convert to uchar
	 *
	 */
  float norm = 255;

  VipsImage* ucharimg;
  if( vips_linear1(image, &ucharimg, norm, (float)0, "uchar", TRUE, NULL) ) {
    vips_error( "imagedisplay",
            "vips_linear1() failed" );
    return( NULL );
  }
  //VIPS_UNREF(image);
  image = ucharimg;


  /* Drop any alpha.
   */
  if( vips_extract_band( image, &x, 0, "n", 3, NULL ) ) {
    g_object_unref( image );
    return( NULL );
  }
  g_object_unref( image );
  image = x;

	x = vips_image_new();
	if( mask )
		*mask = vips_image_new();
	if( vips_sink_screen( image, x, *mask, 128, 128, 400, 0, 
		imagedisplay_render_notify, imagedisplay ) ) {
		g_object_unref( image );
		g_object_unref( x );
		return( NULL );
	}
	g_object_unref( image );
	image = x;

	return( image );
}

static int
imagedisplay_update_conversion( Imagedisplay *imagedisplay )
{
	if( imagedisplay->image ) { 
		if( imagedisplay->srgb_region )
			printf( "** junking region %p\n", 
				imagedisplay->srgb_region );

		VIPS_UNREF( imagedisplay->mask_region );
		VIPS_UNREF( imagedisplay->mask );
		VIPS_UNREF( imagedisplay->srgb_region );
		VIPS_UNREF( imagedisplay->srgb );
		VIPS_UNREF( imagedisplay->display_region );
		VIPS_UNREF( imagedisplay->display );
		VIPS_UNREF( imagedisplay->image_region );

		if( !(imagedisplay->image_region = 
			vips_region_new( imagedisplay->image )) )
			return( -1 ); 

		if( !(imagedisplay->display = 
			imagedisplay_display_image( imagedisplay, 
				imagedisplay->image )) ) 
			return( -1 ); 
		if( !(imagedisplay->display_region = 
			vips_region_new( imagedisplay->display )) )
			return( -1 ); 

		if( !(imagedisplay->srgb = 
			imagedisplay_srgb_image( imagedisplay, 
				imagedisplay->display, &imagedisplay->mask )) ) 
			return( -1 ); 
		if( !(imagedisplay->srgb_region = 
			vips_region_new( imagedisplay->srgb )) )
			return( -1 ); 
		if( !(imagedisplay->mask_region = 
			vips_region_new( imagedisplay->mask )) )
			return( -1 ); 

		printf( "imagedisplay_update_conversion: image size %d x %d\n", 
			imagedisplay->display->Xsize, 
			imagedisplay->display->Ysize );
		printf( "** srgb image %p\n", imagedisplay->srgb );
		printf( "** new region %p\n", imagedisplay->srgb_region );

		gtk_widget_set_size_request( GTK_WIDGET( imagedisplay ),
			imagedisplay->display->Xsize / imagedisplay->device_scale,
			imagedisplay->display->Ysize / imagedisplay->device_scale );

		//gtk_widget_queue_draw( GTK_WIDGET( imagedisplay ) ); 
	}

	return( 0 );
}

static void
imagedisplay_preeval( VipsImage *image, 
	VipsProgress *progress, Imagedisplay *imagedisplay )
{
	g_signal_emit( imagedisplay, 
		imagedisplay_signals[SIG_PRELOAD], 0, progress );
}

static void
imagedisplay_eval( VipsImage *image, 
	VipsProgress *progress, Imagedisplay *imagedisplay )
{
	g_signal_emit( imagedisplay, 
		imagedisplay_signals[SIG_LOAD], 0, progress );
}

static void
imagedisplay_posteval( VipsImage *image, 
	VipsProgress *progress, Imagedisplay *imagedisplay )
{
	g_signal_emit( imagedisplay, 
		imagedisplay_signals[SIG_POSTLOAD], 0, progress );
}


static void
imagedisplay_attach_progress( Imagedisplay *imagedisplay )
{
  g_assert( !imagedisplay->preeval_sig );
  g_assert( !imagedisplay->eval_sig );
  g_assert( !imagedisplay->posteval_sig );

  /* Attach an eval callback: this will tick down if we
   * have to decode this image.
   */
  vips_image_set_progress( imagedisplay->image, TRUE );
  imagedisplay->preeval_sig =
    g_signal_connect( imagedisplay->image, "preeval",
      G_CALLBACK( imagedisplay_preeval ),
      imagedisplay );
  imagedisplay->eval_sig =
    g_signal_connect( imagedisplay->image, "eval",
      G_CALLBACK( imagedisplay_eval ),
      imagedisplay );
  imagedisplay->posteval_sig =
    g_signal_connect( imagedisplay->image, "posteval",
      G_CALLBACK( imagedisplay_posteval ),
      imagedisplay );
}


static void
imagedisplay_cache_image( VipsImage* in, VipsImage** out)
{
  *out = NULL;
  VipsImage* cached;
  int width = in->Xsize;
  int height = in->Ysize;

  //size = (width>height) ? width : height;
  size_t imgsz = VIPS_IMAGE_SIZEOF_PEL(in)*width*height;
  size_t imgmax = 500*1024*1024;
  int memory_storage = (imgsz < imgmax) ? 1 : 0;
  int fd = -1;
  char fname[500]; fname[0] = '\0';

  if( memory_storage ) {
    GTimer *timer;
    gdouble elapsed;
    gulong micros;
    printf("Cache image saved in..."); fflush(stdout);
    timer = g_timer_new ();
    size_t array_sz;
    void* mem_array = vips_image_write_to_memory( in, &array_sz );

    cached = vips_image_new_from_memory( mem_array, array_sz, width, height, in->Bands, in->BandFmt );
    g_signal_connect( cached, "postclose", G_CALLBACK(free_mem_array), mem_array );
    elapsed = g_timer_elapsed (timer, &micros);
    g_timer_destroy (timer);
    printf(" %fs\n", elapsed); fflush(stdout);
  } else {
    char* tempName = vips__temp_name("%s.v");

    vips_image_write_to_file(in, tempName, 0, NULL);

    cached = vips_image_new_from_file(tempName, 0, NULL);
    vips_image_set_delete_on_close(cached, TRUE);

    g_free(tempName);
    g_assert(cached);
  }
  //VIPS_UNREF(in);

  vips_copy( cached, out,
      "format", in->BandFmt,
       "bands", in->Bands,
       "coding", in->Coding,
       "interpretation", in->Type,
       NULL );
  VIPS_UNREF( cached );

  vips_copy_metadata(in, *out);
}

int
imagedisplay_set_file( Imagedisplay *imagedisplay, GFile *file )
{
	imagedisplay_empty( imagedisplay );

	if( file != NULL ) {
		gchar *path;
		gchar *contents;
		gsize length;

		if( (path = g_file_get_path( file )) ) {
			if( !(imagedisplay->image = 
				vips_image_new_from_file( path, "access", VIPS_ACCESS_RANDOM, NULL )) ) {
				g_free( path ); 
				return( -1 );
			}
			g_free( path ); 
		  // Make sure that the sequential access hint is not set for the opened image
		  // see https://github.com/jcupitt/libvips/issues/840
		  vips_image_remove(imagedisplay->image, VIPS_META_SEQUENTIAL);
		}
		else if( g_file_load_contents( file, NULL, 
			&contents, &length, NULL, NULL ) ) {
			if( !(imagedisplay->image =
				vips_image_new_from_buffer( contents, length, 
					"", NULL )) ) {
				g_free( contents );
				return( -1 ); 
			}

			g_signal_connect( imagedisplay->image, "close",
				G_CALLBACK( imagedisplay_close_memory ), 
				contents );
		}
		else {
			vips_error( "imagedisplay", 
				"unable to load GFile object" );
			return( -1 );
		}

    VipsImage* floatimg;
		/**/
    float norm = 1;
    switch(imagedisplay->image->BandFmt) {
    case VIPS_FORMAT_UCHAR: norm = 1.f/255.f; break;
    case VIPS_FORMAT_USHORT: norm = 1.f/USHRT_MAX; break;
    case VIPS_FORMAT_UINT: norm = 1.f/UINT_MAX; break;
    default: break;
    }

    if( vips_linear1(imagedisplay->image, &floatimg, norm, (float)0, NULL) ) {
      vips_error( "imagedisplay",
              "vips_linear1() failed" );
      return -1;
    }
    VIPS_UNREF(imagedisplay->image);
    imagedisplay->image = floatimg;
    /**/

    /* This won't work for CMYK, you need to mess about with ICC profiles
     * for that, but it will work for everything else.
     */
    if( vips_icc_transform_float( imagedisplay->image, &floatimg, linRec2020Profile(), NULL ) ) {
      vips_error( "imagedisplay",
              "vips_icc_transform_float() failed" );
      return -1;
    }
    VIPS_UNREF(imagedisplay->image);
    imagedisplay->image = floatimg;
    /**/

    VipsImage* cached;
    imagedisplay_cache_image(imagedisplay->image, &floatimg);
    VIPS_UNREF(imagedisplay->image);
    imagedisplay->image = floatimg;

#if defined(USE_PYRAMID)
    int size;
    int level_max = 1;
    int width_min = width;
    int height_min = height;
    while(width_min > 256 || height_min > 256) {
      level_max += 1;
      width_min /= 2;
      height_min /= 2;
    }

    if(imagedisplay->pyramid) {
      int i = 0;
      while(imagedisplay->pyramid[i]) {
        VIPS_UNREF(imagedisplay->pyramid[i]);
      }
      g_free(imagedisplay->pyramid);
    }

    imagedisplay->pyramid = g_malloc(sizeof(VipsImage*)*(level_max+1));

    g_object_ref(floatimg);

    imagedisplay->pyramid[0] = floatimg;
    imagedisplay->pyramid[level_max] = NULL;

    printf("Computing pyramid levels...\n"); fflush(stdout);

    int li; for(li = 1; li < level_max; li++) {
      VipsImage* scaled;
      if( vips_shrink( imagedisplay->pyramid[li-1], &scaled, 2, 2, NULL ) ) {
        imagedisplay->pyramid[li] = NULL;
        break;
      }

      width = scaled->Xsize;
      height = scaled->Ysize;

      size = (width>height) ? width : height;
      size_t imgsz = VIPS_IMAGE_SIZEOF_PEL(scaled)*width*height;
      size_t imgmax = 500*1024*1024;
      int memory_storage = (imgsz < imgmax) ? 1 : 0;
      int fd = -1;
      char fname[500]; fname[0] = '\0';

      VipsImage* out;

      if( memory_storage ) {
        GTimer *timer;
        gdouble elapsed;
        gulong micros;
        printf("Level %d image saved in...", li); fflush(stdout);
        timer = g_timer_new ();
        size_t array_sz;
        void* mem_array = vips_image_write_to_memory( scaled, &array_sz );

        out = vips_image_new_from_memory( mem_array, array_sz, width, height, scaled->Bands, scaled->BandFmt );
        g_signal_connect( out, "postclose", G_CALLBACK(free_mem_array), mem_array );
        elapsed = g_timer_elapsed (timer, &micros);
        g_timer_destroy (timer);
        printf(" %fs\n", elapsed); fflush(stdout);
      } else {
        char* tempName = vips__temp_name("%s.v");

        vips_image_write_to_file(scaled, tempName, 0, NULL);

        out = vips_image_new_from_file(tempName, 0, NULL);
        vips_image_set_delete_on_close(imagedisplay->pyramid[li], TRUE);

        g_free(tempName);
        g_assert(out);
      }
      VIPS_UNREF(scaled);

      vips_copy( out, &(imagedisplay->pyramid[li]),
          "format", floatimg->BandFmt,
           "bands", floatimg->Bands,
           "coding", floatimg->Coding,
           "interpretation", floatimg->Type,
           NULL );
      VIPS_UNREF( out );

      vips_copy_metadata(imagedisplay->image, imagedisplay->pyramid[li]);
    }
#endif

    printf(" done\n");

    imagedisplay_attach_progress( imagedisplay );
	}

	imagedisplay_update_conversion( imagedisplay );

	return( 0 );
}

float
imagedisplay_get_mag( Imagedisplay *imagedisplay )
{
	return( imagedisplay->mag );
}

void
imagedisplay_set_mag( Imagedisplay *imagedisplay, float mag )
{
	if( mag > -600 &&
		mag < 1000000 &&
		imagedisplay->mag != mag ) { 
		printf( "imagedisplay_set_mag: %f\n", mag );

		imagedisplay->mag = mag;
		imagedisplay_update_conversion( imagedisplay );
	}
}

gboolean
imagedisplay_get_image_size( Imagedisplay *imagedisplay, 
	int *width, int *height )
{
	if( imagedisplay->image ) {
		*width = imagedisplay->image->Xsize;
		*height = imagedisplay->image->Ysize;

		return( TRUE ); 
	}
	else
		return( FALSE );
}

gboolean
imagedisplay_get_display_image_size( Imagedisplay *imagedisplay, 
	int *width, int *height )
{
	if( imagedisplay->display ) {
		*width = imagedisplay->display->Xsize;
		*height = imagedisplay->display->Ysize;

		return( TRUE ); 
	}
	else
		return( FALSE );
}

/* Map to underlying image coordinates from display image coordinates.
 */
void
imagedisplay_to_image_cods( Imagedisplay *imagedisplay,
	int display_x, int display_y, int *image_x, int *image_y )
{
	if( imagedisplay->mag > 0 ) {
		*image_x = display_x / imagedisplay->mag;
		*image_y = display_y / imagedisplay->mag;
	}
	else {
		*image_x = display_x * -imagedisplay->mag;
		*image_y = display_y * -imagedisplay->mag;
	}
}

/* Map to display cods from underlying image coordinates.
 */
void
imagedisplay_to_display_cods( Imagedisplay *imagedisplay,
	int image_x, int image_y, int *display_x, int *display_y )
{
	if( imagedisplay->mag > 0 ) {
		*display_x = image_x * imagedisplay->mag;
		*display_y = image_y * imagedisplay->mag;
	}
	else {
		*display_x = image_x / -imagedisplay->mag;
		*display_y = image_y / -imagedisplay->mag;
	}
}

VipsPel *
imagedisplay_get_ink( Imagedisplay *imagedisplay, int x, int y )
{
	VipsRect rect;

	rect.left = x;
	rect.top = y;
	rect.width = 1;
	rect.height = 1;
	if( vips_region_prepare( imagedisplay->image_region, &rect ) )
		return( NULL );

	return( VIPS_REGION_ADDR( imagedisplay->image_region, x, y ) );  
}

Imagedisplay *
imagedisplay_new( void ) 
{
	Imagedisplay *imagedisplay;

	printf( "imagedisplay_new:\n" ); 

	imagedisplay = g_object_new( imagedisplay_get_type(),
		NULL );

	return( imagedisplay ); 
}
