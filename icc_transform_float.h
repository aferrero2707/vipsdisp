/* Transform floating-point images with little cms
 *
 * 06/12/17
 * 	- copied from icc_transform.c
 */


/* Has to be before VIPS to avoid nameclashes.
 */
#include <lcms2.h>

#include <vips/vips.h>

/**
 * vips_icc_float_transform: (method)
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
 * Returns: 0 on success, -1 on error.
 */
int
vips_icc_transform_float( VipsImage *in, VipsImage **out,
    cmsHPROFILE output_profile, ... )
__attribute__((sentinel));
