/* Transform floating-point images with little cms
 *
 * 06/12/17
 * 	- copied from icc_transform.c
 */

/*

    This file is part of VIPS.
    
    VIPS is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
    02110-1301  USA

 */

/*

    These files are distributed with VIPS - http://www.vips.ecs.soton.ac.uk

 */

#include "icc_transform_float.h"

#define HAVE_LCMS2

#define MAX_INPUT_IMAGES 64

#ifdef HAVE_LCMS2

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <assert.h>

#include <vips/vips.h>

#ifndef _
#define _
#endif

/* Call lcms with up to this many pixels at once.
 */
#define PIXEL_BUFFER_SIZE (10000)

/**
 * VipsIntent:
 * @VIPS_INTENT_PERCEPTUAL: perceptual rendering intent
 * @VIPS_INTENT_RELATIVE: relative colorimetric rendering intent
 * @VIPS_INTENT_SATURATION: saturation rendering intent
 * @VIPS_INTENT_ABSOLUTE: absolute colorimetric rendering intent
 *
 * The rendering intent. #VIPS_INTENT_ABSOLUTE is best for
 * scientific work, #VIPS_INTENT_RELATIVE is usually best for 
 * accurate communication with other imaging libraries.
 */

/**
 * VipsPCS:
 * @VIPS_PCS_LAB: use CIELAB D65 as the Profile Connection Space
 * @VIPS_PCS_XYZ: use XYZ as the Profile Connection Space
 *
 * Pick a Profile Connection Space for vips_icc_import() and
 * vips_icc_export(). LAB is usually best, XYZ can be more convenient in some 
 * cases. 
 */

/**
 * vips_icc_present:
 *
 * VIPS can optionally be built without the ICC library. Use this function to
 * test for its availability. 
 *
 * Returns: non-zero if the ICC library is present.
 */
int
vips_icc_present( void )
{
	return( 1 );
}


#define VIPS_TYPE_ICC_TRANSFORM_FLOAT (vips_icc_transform_float_get_type())
#define VIPS_ICC_TRANSFORM_FLOAT( obj ) \
  (G_TYPE_CHECK_INSTANCE_CAST( (obj), \
    VIPS_TYPE_ICC_TRANSFORM_FLOAT, VipsIccTransformFloat ))
#define VIPS_ICC_TRANSFORM_FLOAT_CLASS( klass ) \
  (G_TYPE_CHECK_CLASS_CAST( (klass), \
    VIPS_TYPE_ICC_TRANSFORM_FLOAT, VipsIccTransformFloatClass))
#define VIPS_IS_ICC_TRANSFORM_FLOAT( obj ) \
  (G_TYPE_CHECK_INSTANCE_TYPE( (obj), VIPS_TYPE_ICC_TRANSFORM_FLOAT ))
#define VIPS_IS_ICC_TRANSFORM_FLOAT_CLASS( klass ) \
  (G_TYPE_CHECK_CLASS_TYPE( (klass), VIPS_TYPE_ICC_TRANSFORM_FLOAT ))
#define VIPS_ICC_TRANSFORM_FLOAT_GET_CLASS( obj ) \
  (G_TYPE_INSTANCE_GET_CLASS( (obj), \
    VIPS_TYPE_ICC_TRANSFORM_FLOAT, VipsIccTransformFloatClass ))

typedef struct _VipsIccTransformFloat {
  VipsOperation parent_instance;

  /* Input image
   */
  VipsImage *in;

  VipsImage *out;

  VipsIntent intent;

  gboolean embedded;
  char *input_profile_filename;

  cmsHPROFILE in_profile;
  cmsHPROFILE out_profile;
  cmsUInt32Number in_icc_format;
  cmsUInt32Number out_icc_format;
  cmsHTRANSFORM trans;

} VipsIccTransformFloat;

typedef struct _VipsIccTransformFloatClass {
VipsOperationClass parent_class;

} VipsIccTransformFloatClass;

G_DEFINE_TYPE( VipsIccTransformFloat, vips_icc_transform_float, VIPS_TYPE_OPERATION );

GType vips_icc_transform_float_get_type( void );

/* Error from lcms.
 */
static void
icc_error( cmsContext context, cmsUInt32Number code, const char *text )
{
	vips_error( "VipsIccTransformFloat", "%s", text );
}

static void
vips_icc_transform_float_dispose( GObject *gobject )
{
	VipsIccTransformFloat *icc = (VipsIccTransformFloat *) gobject;

	VIPS_FREEF( cmsDeleteTransform, icc->trans );
	VIPS_FREEF( cmsCloseProfile, icc->in_profile );
	/*VIPS_FREEF( cmsCloseProfile, icc->out_profile );*/

	G_OBJECT_CLASS( vips_icc_transform_float_parent_class )->dispose( gobject );
}

/* Is a profile just a pcs stub.
 */
static gboolean
is_pcs( cmsHPROFILE profile )
{
	return( cmsGetColorSpace( profile ) == cmsSigLabData ||
		cmsGetColorSpace( profile ) == cmsSigXYZData ); 
}


static void
vips_check_intent( const char *domain,
  cmsHPROFILE profile, VipsIntent intent, int direction )
{
  if( profile &&
    !cmsIsIntentSupported( profile, intent, direction ) )
    g_warning( _( "%s: intent %d (%s) not supported by "
      "%s profile; falling back to default intent" ),
      domain,
      intent, vips_enum_nick( VIPS_TYPE_INTENT, intent ),
      direction == LCMS_USED_AS_INPUT ?
        _( "input" ) : _( "output" ) );
}


static int
vips_icc_transform_float_profile_needs_bands( cmsHPROFILE profile )
{
  int needs_bands;

  switch( cmsGetColorSpace( profile ) ) {
  case cmsSigGrayData:
    needs_bands = 1;
    break;

  case cmsSigRgbData:
  case cmsSigLabData:
  case cmsSigXYZData:
    needs_bands = 3;
    break;

  case cmsSigCmykData:
    needs_bands = 4;
    break;

  default:
    needs_bands = -1;
    break;
  }

  return( needs_bands );
}

/* How many bands we expect to see from an image after preprocessing by our
 * parent classes. This is a bit fragile :-(
 *
 * FIXME ... split the _build() for colour into separate preprocess / process
 * / postprocess phases so we can load profiles after preprocess but before
 * actual processing takes place.
 */
static int
vips_image_expected_bands( VipsImage *image )
{
  int expected_bands;

  switch( image->Type ) {
  case VIPS_INTERPRETATION_B_W:
  case VIPS_INTERPRETATION_GREY16:
    expected_bands = 1;
    break;

  case VIPS_INTERPRETATION_XYZ:
  case VIPS_INTERPRETATION_LAB:
  case VIPS_INTERPRETATION_LABQ:
  case VIPS_INTERPRETATION_RGB:
  case VIPS_INTERPRETATION_CMC:
  case VIPS_INTERPRETATION_LCH:
  case VIPS_INTERPRETATION_LABS:
  case VIPS_INTERPRETATION_sRGB:
  case VIPS_INTERPRETATION_YXY:
  case VIPS_INTERPRETATION_RGB16:
  case VIPS_INTERPRETATION_scRGB:
  case VIPS_INTERPRETATION_HSV:
    expected_bands = 3;
    break;

  case VIPS_INTERPRETATION_CMYK:
    expected_bands = 4;
    break;

  case VIPS_INTERPRETATION_MULTIBAND:
  case VIPS_INTERPRETATION_HISTOGRAM:
  case VIPS_INTERPRETATION_MATRIX:
  case VIPS_INTERPRETATION_FOURIER:
  default:
    expected_bands = image->Bands;
    break;
  }

  expected_bands = VIPS_MIN( expected_bands, image->Bands );

  return( expected_bands );
}

/* What cmsColorSpaceSignature do we expect this image to be (roughly) after
 * preprocessing. Again, fragile :( see the FIXME above.
 */
static cmsColorSpaceSignature
vips_image_expected_sig( VipsImage *image )
{
  cmsColorSpaceSignature expected_sig;

  switch( image->Type ) {
  case VIPS_INTERPRETATION_B_W:
  case VIPS_INTERPRETATION_GREY16:
    expected_sig = cmsSigGrayData;
    break;

  case VIPS_INTERPRETATION_LAB:
  case VIPS_INTERPRETATION_LABQ:
  case VIPS_INTERPRETATION_LABS:
    expected_sig = cmsSigLabData;
    break;

  case VIPS_INTERPRETATION_sRGB:
  case VIPS_INTERPRETATION_RGB:
  case VIPS_INTERPRETATION_RGB16:
  case VIPS_INTERPRETATION_scRGB:
    expected_sig = cmsSigRgbData;
    break;

  case VIPS_INTERPRETATION_XYZ:
    expected_sig = cmsSigXYZData;
    break;

  case VIPS_INTERPRETATION_CMYK:
    expected_sig = cmsSigCmykData;
    break;

  case VIPS_INTERPRETATION_HSV:
    expected_sig = cmsSigHsvData;
    break;

  case VIPS_INTERPRETATION_YXY:
    expected_sig = cmsSigYxyData;
    break;

  case VIPS_INTERPRETATION_LCH:
  case VIPS_INTERPRETATION_CMC:
  case VIPS_INTERPRETATION_MULTIBAND:
  case VIPS_INTERPRETATION_HISTOGRAM:
  case VIPS_INTERPRETATION_MATRIX:
  case VIPS_INTERPRETATION_FOURIER:
  default:
    expected_sig = -1;
    break;
  }

  return( expected_sig );
}


/* Process a region.
 */
static int
vips_icc_transform_float_gen( VipsRegion *or,
  void *seq, void *a, void *b, gboolean *stop )
{
  VipsRegion *ir = (VipsRegion *) seq;
  VipsIccTransformFloat *icc = VIPS_ICC_TRANSFORM_FLOAT( b );
  VipsIccTransformFloatClass *class = VIPS_ICC_TRANSFORM_FLOAT_GET_CLASS( icc );
  VipsRect *r = &or->valid;

  int i, x, y;
  float *p, *q;
  /*
  printf("vips_icc_transform_float_gen: ir=%p ir->im=%p or=%p or->im=%p\n",
      ir, ir->im, or, or->im);
  printf("vips_icc_transform_float_gen: VIPS_IMAGE_SIZEOF_PEL(ir->im)=%d\n",
      VIPS_IMAGE_SIZEOF_PEL(ir->im));
  printf("vips_icc_transform_float_gen: VIPS_IMAGE_SIZEOF_PEL(or->im)=%d\n",
      VIPS_IMAGE_SIZEOF_PEL(or->im));
  printf("vips_icc_transform_float_gen: r->width=%d  r->height=%d ir->im->Bands=%d or->im->Bands=%d\n",
      r->width, r->height, ir->im->Bands, or->im->Bands);
  printf("vips_icc_transform_float_gen: ir->im->BandFmt=%d or->im->BandFmt=%d\n",
      ir->im->BandFmt, or->im->BandFmt);
  */
  //printf("vips_icc_transform_float_gen: r=%dx%d+%d+%d\n",
  //    r->width, r->height, r->left, r->top);
  if( vips_region_prepare( ir, r ) )
    return( -1 );

  VIPS_GATE_START( "vips_icc_transform_float_gen: work" );

  for( y = 0; y < r->height; y++ ) {
    p = (float*)VIPS_REGION_ADDR( ir, r->left, r->top + y );
    q = (float*)VIPS_REGION_ADDR( or, r->left, r->top + y );

    memcpy(q, p, VIPS_IMAGE_SIZEOF_PEL(ir->im)*r->width);
    //cmsDoTransform( icc->trans, p, q, r->width );
  }

  VIPS_GATE_STOP( "vips_icc_transform_float_gen: work" );

  VIPS_COUNT_PIXELS( or, VIPS_OBJECT_GET_CLASS( icc )->nickname );

  return( 0 );
}


static cmsHPROFILE
vips_icc_transform_float_load_profile_image( VipsImage *image )
{
  void *data;
  size_t data_length;
  cmsHPROFILE profile;

  if( !vips_image_get_typeof( image, VIPS_META_ICC_NAME ) )
    return( NULL );

  if( vips_image_get_blob( image, VIPS_META_ICC_NAME,
    &data, &data_length ) ||
    !(profile = cmsOpenProfileFromMem( data, data_length )) ) {
    g_warning( "%s", _( "corrupt embedded profile" ) );
    return( NULL );
  }

  if( vips_image_expected_bands( image ) !=
    vips_icc_transform_float_profile_needs_bands( profile ) ) {
    VIPS_FREEF( cmsCloseProfile, profile );
    g_warning( "%s",
      _( "embedded profile incompatible with image" ) );
    return( NULL );
  }
  if( vips_image_expected_sig( image ) != cmsGetColorSpace( profile ) ) {
    VIPS_FREEF( cmsCloseProfile, profile );
    g_warning( "%s",
      _( "embedded profile colourspace differs from image" ) );
    return( NULL );
  }

  return( profile );
}

static cmsHPROFILE
vips_icc_transform_float_load_profile_file( const char *domain,
  VipsImage *image, const char *filename )
{
  cmsHPROFILE profile;

  if( !(profile = cmsOpenProfileFromFile( filename, "r" )) ) {
    vips_error( domain,
      _( "unable to open profile \"%s\"" ), filename );
    return( NULL );
  }

  if( vips_image_expected_bands( image ) !=
    vips_icc_transform_float_profile_needs_bands( profile ) ) {
    VIPS_FREEF( cmsCloseProfile, profile );
    g_warning( _( "profile \"%s\" incompatible with image" ),
      filename );
    return( NULL );
  }
  if( vips_image_expected_sig( image ) != cmsGetColorSpace( profile ) ) {
    VIPS_FREEF( cmsCloseProfile, profile );
    g_warning( _( "profile \"%s\" colourspace "
      "differs from image" ), filename );
    return( NULL );
  }

  return( profile );
}


static int
vips_icc_transform_float_attach_profile( VipsImage *im, const char *filename )
{
  char *data;
  size_t data_length;

  if( !(data = vips__file_read_name( filename, NULL,
    &data_length )) )
    return( -1 );
  vips_image_set_blob( im, VIPS_META_ICC_NAME,
    (VipsCallbackFn) g_free, data, data_length );

  return( 0 );
}


static int
vips_icc_transform_float_build( VipsObject *object )
{
  printf("entering vips_icc_transform_float_build\n");
  VipsObjectClass *class = VIPS_OBJECT_GET_CLASS( object );
  VipsIccTransformFloat *icc = (VipsIccTransformFloat *) object;
  VipsIccTransformFloat *transform = (VipsIccTransformFloat *) object;

  if( VIPS_OBJECT_CLASS( vips_icc_transform_float_parent_class )->
    build( object ) )
    return( -1 );

  /* We read the input profile like this:
   *
   *  embedded  filename  action
   *  0   0     image
   *  1   0   image
   *  0   1   file
   *  1   1   image, then fall back to file
   *
   * see also import_build.
   */

  if( icc->in &&
    (transform->embedded ||
      !transform->input_profile_filename) )
    icc->in_profile = vips_icc_transform_float_load_profile_image( icc->in );

  if( !icc->in_profile &&
    icc->in &&
    transform->input_profile_filename )
    icc->in_profile = vips_icc_transform_float_load_profile_file( class->nickname,
      icc->in, transform->input_profile_filename );

  if( !icc->in_profile ) {
    vips_error( class->nickname, "%s", _( "no input profile" ) );
    return( -1 );
  }

  /*
  if( transform->output_profile_size > 0 && transform->output_profile_data ) {
    if( !(icc->out_profile = cmsOpenProfileFromMem(
      transform->output_profile_data, transform->output_profile_size )) ) {
      vips_error( class->nickname,
        _( "unable to open output profile" ) );
      return( -1 );
    }
  }
  */

  vips_check_intent( class->nickname,
    icc->in_profile, icc->intent, LCMS_USED_AS_INPUT );
  vips_check_intent( class->nickname,
    icc->out_profile, icc->intent, LCMS_USED_AS_OUTPUT );


	if( icc->in_profile ) {
		switch( cmsGetColorSpace( icc->in_profile ) ) {
		case cmsSigRgbData:
			icc->in_icc_format = TYPE_RGB_FLT;
			break;

		case cmsSigGrayData:
			icc->in_icc_format = TYPE_GRAY_FLT;
			break;

		case cmsSigCmykData:
			icc->in_icc_format = TYPE_CMYK_FLT;
			break;

		case cmsSigLabData:
			icc->in_icc_format = TYPE_Lab_FLT;
			break;

		case cmsSigXYZData:
			icc->in_icc_format = TYPE_XYZ_FLT;
			break;

		default:
			vips_error( class->nickname, 
				_( "unimplemented input color space 0x%x" ), 
				cmsGetColorSpace( icc->in_profile ) );
			return( -1 );
		}
	}

	if( icc->out_profile ) 
		switch( cmsGetColorSpace( icc->out_profile ) ) {
		case cmsSigRgbData:
			icc->out_icc_format = TYPE_RGB_FLT;
			break;

		case cmsSigGrayData:
			icc->out_icc_format = TYPE_GRAY_FLT;
			break;

		case cmsSigCmykData:
			icc->out_icc_format = TYPE_CMYK_FLT;
			break;

		case cmsSigLabData:
			icc->out_icc_format = TYPE_Lab_FLT;
			break;

		case cmsSigXYZData:
			icc->out_icc_format = TYPE_XYZ_FLT;
			break;

		default:
			vips_error( class->nickname, 
				_( "unimplemented output color space 0x%x" ), 
				cmsGetColorSpace( icc->out_profile ) );
			return( -1 );
		}

	/* At least one must be a device profile.
	 */
	if( icc->in_profile &&
		icc->out_profile &&
		is_pcs( icc->in_profile ) &&
		is_pcs( icc->out_profile ) ) { 
		vips_error( class->nickname,
			"%s", _( "no device profile" ) ); 
		return( -1 );
	}

	/* Use cmsFLAGS_NOCACHE to disable the 1-pixel cache and make
	 * calling cmsDoTransform() from multiple threads safe.
	 */
	if( !(icc->trans = cmsCreateTransform( 
		icc->in_profile, icc->in_icc_format,
		icc->out_profile, icc->out_icc_format, 
		icc->intent, cmsFLAGS_NOOPTIMIZE | cmsFLAGS_NOCACHE )) ) {
	  vips_error( class->nickname,
	        "%s", _( "failed to create transform" ) );
    return( -1 );
	}

	VipsImage* in[2] = {icc->in, NULL};
  if( vips_image_pio_input( in[0] ) )
    return( -1 );

  g_object_ref(icc->in);

  VipsImage* out = vips_image_new();
  if( vips_image_pipeline_array( out,
    VIPS_DEMAND_STYLE_THINSTRIP, in ) ) {
    g_object_unref( out );
    return( -1 );
  }

  out->Coding = in[0]->Coding;
  out->Type = in[0]->Type;
  out->BandFmt = in[0]->BandFmt;
  out->Bands = vips_icc_transform_float_profile_needs_bands( icc->out_profile );

  if( icc->out_profile )
    switch( cmsGetColorSpace( icc->out_profile ) ) {
    case cmsSigRgbData:
      out->Type = VIPS_INTERPRETATION_RGB;
      break;

    case cmsSigGrayData:
      out->Type = VIPS_INTERPRETATION_B_W;
      break;

    case cmsSigCmykData:
      out->Type = VIPS_INTERPRETATION_CMYK;
      break;

    case cmsSigLabData:
      out->Type = VIPS_INTERPRETATION_LAB;
      break;

    case cmsSigXYZData:
      out->Type = VIPS_INTERPRETATION_XYZ;
      break;

    default:
      vips_error( class->nickname,
        _( "unimplemented output color space 0x%x" ),
        cmsGetColorSpace( icc->out_profile ) );
      return( -1 );
    }
  if( icc->out_profile ) {
    void* profile_data;
    cmsUInt32Number profile_size;

    cmsSaveProfileToMem( icc->out_profile, NULL, &profile_size);
    profile_data = malloc( profile_size );
    cmsSaveProfileToMem( icc->out_profile, profile_data, &profile_size);

    vips_image_set_blob( out, VIPS_META_ICC_NAME,
      (VipsCallbackFn) g_free, profile_data, profile_size );
  }

  if( vips_image_generate( out,
    vips_start_one, vips_icc_transform_float_gen, vips_stop_one,
    icc->in, icc ) ) {
    g_object_unref( out );
    return( -1 );
  }

  g_object_set( icc, "out", out, NULL );

  printf("transform build finished: in=%p out=%p\n", in[0], icc->out);

	return( 0 );
}

static void
vips_icc_transform_float_class_init( VipsIccTransformFloatClass *class )
{
  GObjectClass *gobject_class = G_OBJECT_CLASS( class );
  VipsObjectClass *object_class = (VipsObjectClass *) class;
  VipsOperationClass *operation_class = VIPS_OPERATION_CLASS( class );

  gobject_class->dispose = vips_icc_transform_float_dispose;
  gobject_class->set_property = vips_object_set_property;
  gobject_class->get_property = vips_object_get_property;

  object_class->build = vips_icc_transform_float_build;

  object_class->nickname = "icc_float_transform";
  object_class->description = _( "transform using ICC profiles" );
  object_class->build = vips_icc_transform_float_build;

  operation_class->flags = VIPS_OPERATION_SEQUENTIAL;


  VIPS_ARG_IMAGE( class, "in", 1,
    _( "Input" ),
    _( "Input image" ),
    VIPS_ARGUMENT_REQUIRED_INPUT,
    G_STRUCT_OFFSET( VipsIccTransformFloat, in ) );

  VIPS_ARG_ENUM( class, "intent", 6,
    _( "Intent" ),
    _( "Rendering intent" ),
    VIPS_ARGUMENT_OPTIONAL_INPUT,
    G_STRUCT_OFFSET( VipsIccTransformFloat, intent ),
    VIPS_TYPE_INTENT, VIPS_INTENT_RELATIVE );

  VIPS_ARG_IMAGE( class, "out", 100,
    _( "Output" ),
    _( "Output image" ),
    VIPS_ARGUMENT_REQUIRED_OUTPUT,
    G_STRUCT_OFFSET( VipsIccTransformFloat, out ) );

  VIPS_ARG_POINTER( class, "output_profile", 110,
    _( "Output profile" ),
    _( "Memory address of output profile" ),
    VIPS_ARGUMENT_REQUIRED_INPUT,
    G_STRUCT_OFFSET( VipsIccTransformFloat, out_profile ) );

  VIPS_ARG_BOOL( class, "embedded", 120,
    _( "Embedded" ),
    _( "Use embedded input profile, if available" ),
    VIPS_ARGUMENT_OPTIONAL_INPUT,
    G_STRUCT_OFFSET( VipsIccTransformFloat, embedded ),
    FALSE );

  VIPS_ARG_STRING( class, "input_profile", 130,
    _( "Input profile" ),
    _( "Filename to load input profile from" ),
    VIPS_ARGUMENT_OPTIONAL_INPUT,
    G_STRUCT_OFFSET( VipsIccTransformFloat, input_profile_filename ),
    NULL );

  cmsSetLogErrorHandler( icc_error );
}

static void
vips_icc_transform_float_init( VipsIccTransformFloat *icc )
{
	icc->intent = VIPS_INTENT_RELATIVE;
	icc->embedded = TRUE;
  icc->out_profile = NULL;
}


#else /*!HAVE_LCMS2*/


#endif /*HAVE_LCMS*/


/**
 * vips_icc_transform_float: (method)
 * @in: input image
 * @out: (out): output image
 * @output_profile: get the output profile from here
 * @...: %NULL-terminated list of optional named arguments
 *
 * Optional arguments:
 *
 * * @input_profile: get the input profile from here
 * * @intent: transform with this intent
 * * @depth: depth of output image in bits
 * * @embedded: use profile embedded in input image
 *
 * Transform an image with a pair of ICC profiles. The input image is moved to
 * profile-connection space with the input profile and then to the output
 * space with the output profile.
 *
 * If @embedded is set, the input profile is taken from the input image
 * metadata, if present. If there is no embedded profile,
 * @input_profile is used as a fall-back.
 * You can test for the
 * presence of an embedded profile with
 * vips_image_get_typeof() with #VIPS_META_ICC_NAME as an argument. This will
 * return %GType 0 if there is no profile. 
 *
 * If @embedded is not set, the input profile is taken from
 * @input_profile. If @input_profile is not supplied, the
 * metadata profile, if any, is used as a fall-back. 
 *
 * The output image has the output profile attached to the #VIPS_META_ICC_NAME
 * field. 
 *
 * Use vips_icc_float_import() and vips_icc_float_export() to do either the first or
 * second half of this operation in isolation.
 *
 * Returns: 0 on success, -1 on error.
 */
int
vips_icc_transform_float( VipsImage *in, VipsImage **out,
    cmsHPROFILE output_profile, ... )
{
	va_list ap;
	int result;

	va_start( ap, output_profile );
	result = vips_call_split( "icc_float_transform", ap,
		in, out, output_profile );
	va_end( ap );

	return( result );
}
