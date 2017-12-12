/* Display an image with gtk3 and libvips. 
 */

#include <stdio.h>
#include <stdlib.h>

#include <gtk/gtk.h>

#include <vips/vips.h>

#include "disp.h"

GType vips_icc_transform_float_get_type( void );

void benchmark_file(gchar* path)
{
  printf("Processing %s\n", path);
  gchar *contents;
  gsize length;

  VipsImage* image;

  if( !(image = vips_image_new_from_file( path, NULL )) ) {
    vips_error( "disp",
      "unable to load image" );
    return;
  }
  /*else if( g_file_load_contents( file, NULL,
    &contents, &length, NULL, NULL ) ) {
    if( !(image = vips_image_new_from_buffer( contents, length,
        "", NULL )) ) {
      g_free( contents );
      return;
    }

    g_signal_connect( image, "close",
      G_CALLBACK( imagedisplay_close_memory ),
      contents );
  }*/
  printf("%s opened\n", path);

  float norm = 1;
  switch(image->BandFmt) {
  case VIPS_FORMAT_UCHAR: norm = 1.f/255.f; break;
  case VIPS_FORMAT_USHORT: norm = 1.f/USHRT_MAX; break;
  case VIPS_FORMAT_UINT: norm = 1.f/UINT_MAX; break;
  default: break;
  }

  VipsImage* floatimg;
  if( vips_linear1(image, &floatimg, norm, (float)0, NULL) ) {
    vips_error( "imagedisplay",
            "vips_linear1() failed" );
    return;
  }
  VIPS_UNREF(image);
  image = floatimg;


  /* This won't work for CMYK, you need to mess about with ICC profiles
   * for that, but it will work for everything else.
   */
  if( vips_icc_transform_float( image, &floatimg, "Rec2020-elle-V4-g10.icc", NULL ) ) {
    vips_error( "imagedisplay",
            "vips_icc_transform_float() failed" );
    return;
  }
  VIPS_UNREF(image);
  image = floatimg;


  int width = image->Xsize;
  int height = image->Xsize;
  int size;

  g_object_ref(floatimg);

  printf("Computing scaled image...");

  VipsImage* scaled;
  //if( vips_shrink( image, &scaled, 16, 16, NULL ) ) {
    if( vips_resize( image, &scaled, 0.7, NULL ) ) {
    vips_error( "imagedisplay",
                "vips_shrink() failed" );
    return;
  }

  width = scaled->Xsize;
  height = scaled->Ysize;
  size = (width>height) ? width : height;

  size_t imgsz = VIPS_IMAGE_SIZEOF_PEL(scaled)*width*height;
  size_t imgmax = 500*1024*1024;

  size_t array_sz;
  void* mem_array = vips_image_write_to_memory( scaled, &array_sz );

  if( mem_array ) g_free(mem_array);
}

int
main( int argc, char **argv )
{
	Disp *disp;
	int status;
	const gchar *accels[] = { "F11", NULL };

	if( VIPS_INIT( argv[0] ) )
		vips_error_exit( "unable to start VIPS" );

	vips_icc_transform_float_get_type();

	//vips_concurrency_set(1);
	printf("VIPS cpmcurrency: %d\n", vips_concurrency_get());


	benchmark_file(argv[1]);

	return 0;
}
