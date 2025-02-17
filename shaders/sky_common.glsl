#include "utils.glsl"

const int Slot_Parameters = 0;
const int Slot_TransmittanceLutRW = 1;
const int Slot_SkyViewLutRW = 2;
//...
const int Slot_TransmittanceLutR = 8;
const int Slot_SkyViewLutR = 9;


struct AtmosphereProfile {

    vec3 rayleighScattering;
    float bottomRadius;

    vec3 mieScattering;
    float topRadius;

    vec3 mieAbsorption;
    float miePhaseG;

    vec3 ozoneAbsorption;
    float ozoneMeanHeight;

    vec3 groundAlbedo; // not used for now
    float ozoneLayerWidth;
};

layout(set = 1, binding = Slot_Parameters) uniform SkyAtmosphereParamsBufferObject {

    AtmosphereProfile atmosphere;

    vec3 cameraPosES;
    float exposure;

    vec3 dir2sun;
    float sunAngularRadius;

    vec2 skyViewNumSamplesMinMax;

    uvec2 transmittanceLutTextureDimensions;
    uvec2 skyViewLutTextureDimensions;

} params;

layout(set = 1, binding = Slot_TransmittanceLutR) uniform sampler2D transmittanceLut;
layout(set = 1, binding = Slot_SkyViewLutR) uniform sampler2D skyViewLut;

/////////////////////////////////// Internal fn ///////////////////////////////////////

struct AtmosphereSample {
    vec3 rayleighScattering;
    vec3 mieScattering;
    vec3 scattering;
    vec3 absorption;
};

vec3 projectToPerpendicularPlane(vec3 v, vec3 n) {
    vec3 parallelComponent = dot(v, n) * n;
    return v - parallelComponent;
}

vec2 UvToTransmittanceLutParams(float bottomRadius, float topRadius, vec2 uv) {
    float x_mu = uv.x;
    float x_r = uv.y;

    float viewHeight;
    float viewZenithCosAngle;

    float H = sqrt(topRadius * topRadius - bottomRadius * bottomRadius);
    float rho = H * x_r;
    viewHeight = sqrt(rho * rho + bottomRadius * bottomRadius);

    float d_min = topRadius - viewHeight;
    float d_max = rho + H;
    float d = d_min + x_mu * (d_max - d_min);

    // well ok. makes geometric sense now. made a tiny change to show I understood the code..
    viewZenithCosAngle = d == 0.0 ? 1.0f : (topRadius * topRadius - viewHeight * viewHeight - d * d) / (2.0 * viewHeight * d);
    viewZenithCosAngle = clamp(viewZenithCosAngle, -1.0f, 1.0f);

    return vec2(viewHeight, viewZenithCosAngle);
}

vec2 TransmittanceLutParamsToUv(float bottomRadius, float topRadius, float viewHeight, float viewZenithCosAngle) {
    float discriminant = viewHeight * viewHeight * (viewZenithCosAngle * viewZenithCosAngle - 1.0) + topRadius * topRadius;
    float d = max(0.0f, (-viewHeight * viewZenithCosAngle + sqrt(discriminant))); // Distance to atmosphere boundary

    float H = sqrt(max(0.0f, topRadius * topRadius - bottomRadius * bottomRadius));
    float rho = sqrt(max(0.0f, viewHeight * viewHeight - bottomRadius * bottomRadius));

    float d_min = topRadius - viewHeight;
    float d_max = rho + H;
    float x_mu = (d - d_min) / (d_max - d_min);
    float x_r = rho / H;

    return vec2(x_mu, x_r);
}

float toSubUv(float u, float resolution) {
    return u - u / resolution + 0.5f / resolution;
}

float fromSubUv(float x, float resolution) {
    return (x * resolution - 0.5f) / (resolution - 1.0f);
}

#define SKYVIEWLUT_LINEAR_MAPPING 0

vec2 SkyViewLutParamsToUv(vec3 cameraPosES, vec3 dir2sun, vec3 viewDir, float bottomRadius, bool intersectGround) {
    vec2 uv;
    vec3 dir2zenith = normalize(cameraPosES);

#if SKYVIEWLUT_LINEAR_MAPPING

    vec3 dir2sunHorizontal = projectToPerpendicularPlane(dir2sun, dir2zenith);
    if (dir2sunHorizontal.x == 0 && dir2sunHorizontal.y == 0 && dir2sunHorizontal.z == 0) {
        // sun is straight above head (hack)
        dir2sunHorizontal.x = 1;
    } else {
        dir2sunHorizontal = normalize(dir2sunHorizontal);
    }

    vec3 viewDirHorizontal = projectToPerpendicularPlane(viewDir, dir2zenith);
    if (viewDirHorizontal.x == 0 && viewDirHorizontal.y == 0 && viewDirHorizontal.z == 0) {
        // looking straight above head (hack)
        viewDirHorizontal.x = 1;
    } else {
        viewDirHorizontal = normalize(viewDirHorizontal);
    }

    // non-linear mapping can transform uv here depending on the sign of dot(viewDir, dir2zenith),
    // but when decoding uv there would be no way to know where the division happens...
    // in other words there's no single transform function that applies, because the horizon could appear anywhere
    uv.x = -dot(viewDirHorizontal, dir2sunHorizontal) * 0.5f + 0.5f;
    uv.y = -dot(viewDir, dir2zenith) * 0.5f + 0.5f;
#else
    float viewHeight = length(cameraPosES);
    float viewZenithCosine = min(1.0f, dot(dir2zenith, viewDir));

    float Vhorizon = sqrt(max(0, viewHeight * viewHeight - bottomRadius * bottomRadius));
    float CosBeta = min(1.0f, Vhorizon / viewHeight);				// GroundToHorizonCos
    float Beta = acos(CosBeta);
    float ZenithHorizonAngle = PI - Beta;

    if (!intersectGround) {
        float coord = acos(viewZenithCosine) / ZenithHorizonAngle;
        coord = 1.0f - coord;
        coord = sqrt(max(0, coord));
        coord = 1.0f - coord;
        uv.y = coord * 0.5f;
    } else {
        float coord = (acos(viewZenithCosine) - ZenithHorizonAngle) / Beta;
        coord = sqrt(max(0, coord));
        uv.y = coord * 0.5f + 0.5f;
    }

    {
        float coord = -(dot(dir2sun, viewDir)) * 0.5f + 0.5f;
        coord = sqrt(max(0, coord));
        uv.x = coord;
    }

    // Constrain uvs to valid sub texel range (avoid zenith derivative issue making LUT usage visible)
    //uv = float2(fromUnitToSubUvs(uv.x, 192.0f), fromUnitToSubUvs(uv.y, 108.0f));
#endif
    return uv;
}

void UvToSkyViewLutParams(
vec2 uv, float bottomRadius, vec3 cameraPosES, vec3 dir2sun, out vec3 cameraPosESProxy, out vec3 viewDirProxy, out vec3 dir2sunProxy) {

#if SKYVIEWLUT_LINEAR_MAPPING // straight-forward linear mapping
    float viewSunCosine = 1.0f - uv.x * 2.0f; // horizontal, [1, -1]
    float viewZenithCosine = 1.0f - uv.y * 2.0f; // vertical, [1, -1]

#else // non-linear mapping from the sample, which keeps horizon in the middle
    // Constrain uvs to valid sub texel range (avoid zenith derivative issue making LUT usage visible)
    //uv = float2(fromSubUvsToUnit(uv.x, 192.0f), fromSubUvsToUnit(uv.y, 108.0f));

    float viewHeight = length(cameraPosES);

    float Vhorizon = sqrt(viewHeight * viewHeight - bottomRadius * bottomRadius);
    float CosBeta = Vhorizon / viewHeight;				// GroundToHorizonCos
    float Beta = acos(CosBeta);
    float ZenithHorizonAngle = PI - Beta;

    float viewZenithCosine;
    if (uv.y < 0.5f) {
        float coord = 2.0*uv.y;
        coord = 1.0 - coord;
        coord *= coord;
        coord = 1.0 - coord;
        viewZenithCosine = cos(ZenithHorizonAngle * coord);
    } else {
        float coord = uv.y * 2.0f - 1.0f;
        coord *= coord;
        viewZenithCosine = cos(ZenithHorizonAngle + Beta * coord);
    }

    float coord = uv.x;
    coord *= coord;
    float viewSunCosine = -(coord * 2.0f - 1.0f);
#endif
    float sunZenithCosine = dot(dir2sun, normalize(cameraPosES));

    float viewZenithSine = sqrt(1.0f - viewZenithCosine * viewZenithCosine);
    float viewSunSine = sqrt(1.0f - viewSunCosine * viewSunCosine);

    dir2sunProxy = normalize(vec3(sqrt(1.0f - sunZenithCosine * sunZenithCosine), 0, sunZenithCosine));
    cameraPosESProxy = vec3(0.0f, 0.0f, length(cameraPosES));
    viewDirProxy = normalize(vec3(
    viewZenithSine * viewSunCosine,
    viewZenithSine * viewSunSine,
    viewZenithCosine));
}

// took this straight from sample code because I'm too lazy to write my own
// - r0: ray origin
// - rd: normalized ray direction
// - s0: sphere center
// - sR: sphere radius
// - Returns distance from r0 to first intersecion with sphere,
//   or -1.0 if no intersection.
float raySphereIntersectNearest(vec3 r0, vec3 rd, vec3 s0, float sR) {
    float a = dot(rd, rd);
    vec3 s0_r0 = r0 - s0;
    float b = 2.0f * dot(rd, s0_r0);
    float c = dot(s0_r0, s0_r0) - (sR * sR);
    float delta = b * b - 4.0 * a * c;
    if (delta < 0.0 || a == 0.0) {
        return -1.0;
    }
    float sol0 = (-b - sqrt(delta)) / (2.0f * a);
    float sol1 = (-b + sqrt(delta)) / (2.0f * a);
    if (sol0 < 0.0 && sol1 < 0.0) {
        return -1.0;
    }
    if (sol0 < 0.0) {
        return max(0.0f, sol1);
    } else if (sol1 < 0.0) {
        return max(0.0f, sol0);
    }
    return max(0.0f, min(sol0, sol1));
}

float computeRayleighPhase(float cosTheta) {
    const float denom = 16.0f * PI;
    return 3 * (1 + cosTheta * cosTheta) / denom;
}

float computeHgPhase(float g, float cosTheta) {
#if 0
    // https://omlc.org/classroom/ece532/class3/hg.html
    // seems wrong though (not normalized)
    float gSq = g * g;
    float denom = pow(1 + gSq - 2*g * cosTheta, 1.5f);
    return 0.5f * (1 - gSq) / denom;
#else
    // took straight from the sample
    float k = 3.0 / (8.0 * PI) * (1.0 - g * g) / (2.0 + g * g);
    return k * (1.0 + cosTheta * cosTheta) / pow(1.0 + g * g - 2.0 * g * -cosTheta, 1.5);
#endif
}

AtmosphereSample sampleAtmosphere(AtmosphereProfile atmosphere, float heightFromGroundKM) {
    heightFromGroundKM = max(0.0f, heightFromGroundKM); // if underground, clamp to ground.

    float rayleighDensity = exp(-heightFromGroundKM * 0.125f);
    float mieDensity = exp(-heightFromGroundKM * 0.833f);
    float distToMeanOzoneHeight = abs(heightFromGroundKM - atmosphere.ozoneMeanHeight);
    float halfOzoneLayerWidth = atmosphere.ozoneLayerWidth * 0.5f;
    float ozoneDensity = max(0.0f, halfOzoneLayerWidth - distToMeanOzoneHeight) / halfOzoneLayerWidth;

    AtmosphereSample s;
    s.rayleighScattering = rayleighDensity * atmosphere.rayleighScattering;
    s.mieScattering = mieDensity * atmosphere.mieScattering;
    s.scattering = s.rayleighScattering + s.mieScattering; // ozone does not scatter
    s.absorption =
        // rayleigh does not absorb
        mieDensity * atmosphere.mieAbsorption +
        ozoneDensity * atmosphere.ozoneAbsorption;

    return s;
}
// assume sample pos is within the atmosphere already
vec3 computeTransmittanceToSun(AtmosphereProfile atmosphere, float viewHeight, float viewZenithCosine) {

    float discriminant = viewHeight * viewHeight * (viewZenithCosine * viewZenithCosine - 1.0) + atmosphere.topRadius * atmosphere.topRadius;
    float distToAtmosphereTop = max(0.0f, (-viewHeight * viewZenithCosine + sqrt(discriminant))); // Distance to atmosphere boundary

    vec3 traceStartPosES = vec3(0, 0, viewHeight);
    vec3 traceDir = vec3(sqrt(1.0f - viewZenithCosine * viewZenithCosine), 0, viewZenithCosine);

    vec3 cumOpticalDepth = vec3(0, 0, 0);

    float numSamplesF = 40;
    const float sampleSegmentT = 0.3f;
    float t = 0;

    for (float i = 0; i < numSamplesF; i += 1.0f) {
        float newT = distToAtmosphereTop * ((i + sampleSegmentT) / numSamplesF);
        float dt = newT - t;
        t = newT;
        vec3 samplePosES = traceStartPosES + t * traceDir;
        AtmosphereSample s = sampleAtmosphere(atmosphere, length(samplePosES) - atmosphere.bottomRadius);
        vec3 segmentOpticalDepth = dt * (s.scattering + s.absorption);
        cumOpticalDepth += segmentOpticalDepth;
    }

    return exp(-cumOpticalDepth);
}
vec3 sampleTransmittanceToSun(float bottomRadius, float topRadius, float viewHeight, float viewZenithCosine) {
    vec2 uv = TransmittanceLutParamsToUv(bottomRadius, topRadius, viewHeight, viewZenithCosine);
    vec4 texel = textureLod(transmittanceLut, uv, 0);
    return vec3(texel.r, texel.g, texel.b);
}

vec2 computeRaymarchAtmosphereMinMaxT(vec3 startPosES, vec3 raymarchDir, vec3 earthCenterES, float bottomRadius, float topRadius) {
    // get the range to raymarch through
    const float tBottom = raySphereIntersectNearest(startPosES, raymarchDir, earthCenterES, bottomRadius);
    const float tTop = raySphereIntersectNearest(startPosES, raymarchDir, earthCenterES, topRadius);
    const bool isectWithGround = tBottom >= 0;
    const bool isectWithTop = tTop >= 0;
    float tMin, tMax;
    if (isectWithGround) {
        if (isectWithTop) {
            float viewHeight = length(startPosES);
            if (viewHeight < bottomRadius) {
                // underground
                tMin = 0;
                tMax = tTop;
            } else if (viewHeight < topRadius) {
                // within atmosphere, looking down
                tMin = 0;
                tMax = tBottom;
            } else {
                // from outer space, looking at planet
                tMin = tTop;
                tMax = tBottom;
            }
        } else {
            // shouldn't get here (no isect with top but isect with ground is impossible)
        }
    } else { // not intersecting w ground
        if (isectWithTop) {
            float viewHeight = length(startPosES);
            if (viewHeight < topRadius) {
                // within atmosphere, looking up into the sky
                tMin = 0;
                tMax = tTop;
            } else {
                // from outer space, view ray penetrate through atmosphere and to outer space again
                tMin = tTop;
                tMax = raySphereIntersectNearest(
                startPosES + raymarchDir * topRadius * 2.01f,
                -raymarchDir,
                earthCenterES,
                topRadius);
            }
        } else {
            // in outer space, not looking at planet
            tMin = -1;
            tMax = -1;
        }
    }
    return vec2(tMin, tMax);
}
vec3 computeSkyAtmosphere(
    AtmosphereProfile atmosphere,
    vec3 cameraPosES,
    vec3 viewDir,
    vec3 dir2sun,
    vec3 sunLuminance,
    vec2 numSamplesMinMax)
{
    vec3 earthCenterES = vec3(0, 0, 0);

    vec2 tMinMax = computeRaymarchAtmosphereMinMaxT(cameraPosES, viewDir, earthCenterES, atmosphere.bottomRadius, atmosphere.topRadius);
    bool shouldRaymarch = tMinMax.x >= 0 && tMinMax.y >= 0;

    if (shouldRaymarch) {

        // prepare to actually raymarch: let [tmin, tmax] be [0, length of entire raymarch segment]
        vec3 raymarchStartPosES = cameraPosES + tMinMax.x * viewDir;
        float tMax = tMinMax.y - tMinMax.x;

        // phase functions
        float cosTheta_viewDir_dir2sun = dot(viewDir, dir2sun);
        float rayleighPhase = computeRayleighPhase(cosTheta_viewDir_dir2sun);
        float miePhase = computeHgPhase(atmosphere.miePhaseG, -cosTheta_viewDir_dir2sun);

        // todo: wtf is this
        //tMax = min(tMax, 200.0f);

        vec3 L = vec3(0, 0, 0);
        vec3 sunContribThroughput = vec3(1, 1, 1);

        #if 0// fixed samples count
        float numSamplesF = 64.0f;
        #else
        float upness = 1.0f - clamp(dot(normalize(cameraPosES), viewDir), 0.0f, 1.0f);
        float numSamplesF = floor(mix(numSamplesMinMax.x, numSamplesMinMax.y, upness));
        #endif
        const float sampleSegmentT = 0.3f;
        float t = 0;

        for (float i = 0; i < numSamplesF; i += 1.0f) {
            // loop variables
            #if 0
            float newT01 = (i + sampleSegmentT) / numSamplesF;
            float newT = tMax * newT01;
            float dt = newT - t;
            t = newT;
            #else// non-linear distribution of samples along the raymarching path (not seeing any difference.. yet)
            // normalized segment range
            float t0 = i / numSamplesF;
            float t1 = (i + 1) / numSamplesF;
            // make them non-linear
            t0 = t0 * t0;
            t1 = t1 * t1;
            // make them world-scale
            t0 *= tMax;
            t1 = min(tMax, t1 * tMax);
            t = t0 + (t1 - t0) * sampleSegmentT;
            float dt = t1 - t0;
            #endif
            vec3 samplePosES = cameraPosES + t * viewDir;

            // atmosphere sample
            AtmosphereSample atmosphereSample = sampleAtmosphere(atmosphere, length(samplePosES) - atmosphere.bottomRadius);

            float viewHeight = length(samplePosES);
            #if 1// use transmittance lut
            vec3 transmittanceToSun = sampleTransmittanceToSun(
                atmosphere.bottomRadius,
                atmosphere.topRadius,
                viewHeight,
                dot(samplePosES, dir2sun) / viewHeight);
            #else
            vec3 transmittanceToSun = computeTransmittanceToSun(atmosphere, viewHeight, dot(samplePosES, dir2sun) / viewHeight);
            #endif

            vec3 phaseTimesScattering = atmosphereSample.mieScattering * miePhase + atmosphereSample.rayleighScattering * rayleighPhase;

            // earth shadow
            float tEarth = raySphereIntersectNearest(samplePosES, dir2sun, earthCenterES, atmosphere.bottomRadius);
            float sunVisibility = tEarth >= 0 ? 0 : 1;

            // todo: add multiscattered light contribution here
            vec3 sunContrib = sunLuminance * sunVisibility * transmittanceToSun * phaseTimesScattering;

            vec3 segmentOpticalDepth = (atmosphereSample.scattering + atmosphereSample.absorption) * dt;
            vec3 segmentTransmittance = exp(-segmentOpticalDepth);

            // analytical solution to the integral along view ray segment (see sample code)
            L += sunContribThroughput * (sunContrib - sunContrib * segmentTransmittance) /
                (atmosphereSample.scattering + atmosphereSample.absorption);

            sunContribThroughput *= segmentTransmittance;

        }// end for (raymarch along view ray)

        return L;

    }// end if (shouldRaymarch)

    return vec3(0, 0, 0);
}

/////////////////////////////////// Call this ////////////////////////////////////////

vec3 sampleSkyAtmosphere(vec3 viewDir) {
    float bottomRadius = params.atmosphere.bottomRadius;
    vec3 cameraPosES = params.cameraPosES;
    bool intersectGround = raySphereIntersectNearest(cameraPosES, viewDir, vec3(0, 0, 0), bottomRadius) >= 0;
    vec2 skyViewUv = SkyViewLutParamsToUv(cameraPosES, params.dir2sun, viewDir, bottomRadius, intersectGround);
    skyViewUv = vec2(toSubUv(skyViewUv.x, params.skyViewLutTextureDimensions.x), toSubUv(skyViewUv.y, params.skyViewLutTextureDimensions.y));

    vec3 L = textureLod(skyViewLut, skyViewUv, 0).rgb;

    // sun disc
    if (dot(viewDir, params.dir2sun) >= cos(params.sunAngularRadius)) {
        vec3 transmittanceToSun = sampleTransmittanceToSun(
            params.atmosphere.bottomRadius,
            params.atmosphere.topRadius,
            length(cameraPosES),
            dot(viewDir, normalize(cameraPosES)));
        float sunVisibility = intersectGround ? 0.0f : 1.0f;
        L += transmittanceToSun * sunVisibility; // assumes sun luminance is 1
    }

    // post process it
    vec3 white_point = vec3(1.08241, 0.96756, 0.95003);
    L = 1.0f - exp(-vec3(L.x, L.y, L.z) / white_point * params.exposure);

    return L;
}
