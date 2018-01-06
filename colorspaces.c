#include <colorspaces.h>

/* ***** Make profile: sRGB, D65, sRGB TRC */
/* http://en.wikipedia.org/wiki/Srgb */
/* Hewlett-Packard and Microsoft designed sRGB to match
 * the color gamut of consumer-grade CRTs from the 1990s
 * and to be the standard color space for the world wide web.
 * When made using the standard sRGB TRC, this sRGB profile
 * can be applied to DCF R03 camera-generated jpegs and
 * is an excellent color space for editing 8-bit images.
 * When made using the linear gamma TRC, the resulting profile
 * should only be used for high bit depth image editing.
 * */
static cmsCIExyYTRIPLE srgb_primaries = {
    {0.6400, 0.3300, 1.0},
    {0.3000, 0.6000, 1.0},
    {0.1500, 0.0600, 1.0}
};

static cmsCIExyYTRIPLE srgb_primaries_pre_quantized = {
    {0.639998686, 0.330010138, 1.0},
    {0.300003784, 0.600003357, 1.0},
    {0.150002046, 0.059997204, 1.0}
};


static cmsCIExyYTRIPLE rec2020_primaries = {
    {0.7079, 0.2920, 1.0},
    {0.1702, 0.7965, 1.0},
    {0.1314, 0.0459, 1.0}
};

static cmsCIExyYTRIPLE rec2020_primaries_prequantized = {
    {0.708012540607, 0.291993664388, 1.0},
    {0.169991652439, 0.797007778423, 1.0},
    {0.130997824007, 0.045996550894, 1.0}
};

/* ************************** WHITE POINTS ************************** */

/* D65 WHITE POINTS */

static cmsCIExyY  d65_srgb_adobe_specs = {0.3127, 0.3290, 1.0};
/* White point from the sRGB.icm and AdobeRGB1998 profile specs:
 * http://www.adobe.com/digitalimag/pdfs/AdobeRGB1998.pdf
 * 4.2.1 Reference Display White Point
 * The chromaticity coordinates of white displayed on
 * the reference color monitor shall be x=0.3127, y=0.3290.
 * . . . [which] correspond to CIE Standard Illuminant D65.
 *
 * Wikipedia gives this same white point for SMPTE-C.
 * This white point is also given in the sRGB color space specs.
 * It's probably correct for most or all of the standard D65 profiles.
 *
 * The D65 white point values used in the LCMS virtual sRGB profile
 * is slightly different than the D65 white point values given in the
 * sRGB color space specs, so the LCMS virtual sRGB profile
 * doesn't match sRGB profiles made using the values given in the
 * sRGB color space specs.
 *
 * */

cmsHPROFILE sRGBProfile()
{
  static cmsHPROFILE srgb = NULL;
  if( srgb ) return srgb;

  /* ***** Make profile: sRGB, D65, sRGB TRC */
  /*
   * */
  cmsCIExyYTRIPLE primaries = srgb_primaries_pre_quantized;
  cmsCIExyY whitepoint = d65_srgb_adobe_specs;
  cmsToneCurve* tone_curve[3];
  /* sRGB TRC */
  cmsFloat64Number srgb_parameters[5] =
  { 2.4, 1.0 / 1.055,  0.055 / 1.055, 1.0 / 12.92, 0.04045 };
  cmsToneCurve *curve = cmsBuildParametricToneCurve(NULL, 4, srgb_parameters);
  //cmsToneCurve *curve = cmsBuildTabulatedToneCurve16(NULL, dt_srgb_tone_curve_values_n, dt_srgb_tone_curve_values);
  tone_curve[0] = tone_curve[1] = tone_curve[2] = curve;

  cmsHPROFILE profile = cmsCreateRGBProfile ( &whitepoint, &primaries, tone_curve );
  cmsMLU *copyright = cmsMLUalloc(NULL, 1);
  cmsMLUsetASCII(copyright, "en", "US", "Copyright 2015, Elle Stone (website: http://ninedegreesbelow.com/; email: ellestone@ninedegreesbelow.com). This ICC profile is licensed under a Creative Commons Attribution-ShareAlike 3.0 Unported License (https://creativecommons.org/licenses/by-sa/3.0/legalcode).");
  cmsWriteTag(profile, cmsSigCopyrightTag, copyright);
  /* V4 */
  cmsMLU *description = cmsMLUalloc(NULL, 1);
  cmsMLUsetASCII(description, "en", "US", "sRGB-elle-srgbtrc-V4.icc");
  cmsWriteTag(profile, cmsSigProfileDescriptionTag, description);
  cmsMLUfree(description);

  srgb = profile;
  return srgb;
}


cmsHPROFILE linRec2020Profile()
{
  static cmsHPROFILE profile = NULL;
  if( profile ) return profile;

  /* ***** Make profile: Rec.2020, D65, linear TRC */
  /*
   * */
  cmsCIExyYTRIPLE primaries = rec2020_primaries_prequantized;
  cmsCIExyY whitepoint = d65_srgb_adobe_specs;
  cmsToneCurve* tone_curve[3];

  cmsToneCurve *curve = cmsBuildGamma (NULL, 1.00);
  tone_curve[0] = tone_curve[1] = tone_curve[2] = curve;

  profile = cmsCreateRGBProfile ( &whitepoint, &primaries, tone_curve );
  cmsMLU *copyright = cmsMLUalloc(NULL, 1);
  cmsMLUsetASCII(copyright, "en", "US", "Copyright 2015, Elle Stone (website: http://ninedegreesbelow.com/; email: ellestone@ninedegreesbelow.com). This ICC profile is licensed under a Creative Commons Attribution-ShareAlike 3.0 Unported License (https://creativecommons.org/licenses/by-sa/3.0/legalcode).");
  cmsWriteTag(profile, cmsSigCopyrightTag, copyright);
  // V4
  cmsMLU *description = cmsMLUalloc(NULL, 1);
  cmsMLUsetASCII(description, "en", "US", "Rec2020-elle-g1.0-V4.icc");
  cmsWriteTag(profile, cmsSigProfileDescriptionTag, description);
  cmsMLUfree(description);

  return profile;
}
