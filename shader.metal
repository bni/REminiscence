#include <metal_stdlib>
using namespace metal;

// Uniform buffer for shader parameters
struct Params {
    float trg_x;
    float trg_y;
    float trg_w;
    float trg_h;
    float scr_w;
    float scr_h;
    float res_x;
    float res_y;
};

// Emulated input resolution.
constant float2 res=float2(256.0,224.0);

// Hardness of scanline.
//  -8.0 = soft
// -16.0 = medium
constant float hardScan=-8.0;

// Hardness of pixels in scanline.
// -2.0 = soft
// -4.0 = hard
constant float hardPix=-3.0;

// Display warp.
// 0.0 = none
// 1.0/8.0 = extreme
constant float2 warp= float2(0.0,0.0);

// Amount of shadow mask.
constant float maskDark=0.5;
constant float maskLight=1.5;

//------------------------------------------------------------------------

// sRGB to Linear.
// Assuing using sRGB typed textures this should not be needed.
float ToLinear1(float c){return(c<=0.04045)?c/12.92:pow((c+0.055)/1.055,2.4);}
float3 ToLinear(float3 c){return float3(ToLinear1(c.r),ToLinear1(c.g),ToLinear1(c.b));}

// Linear to sRGB.
// Assuing using sRGB typed textures this should not be needed.
float ToSrgb1(float c){return(c<0.0031308?c*12.92:1.055*pow(c,0.41666)-0.055);}
float3 ToSrgb(float3 c){return float3(ToSrgb1(c.r),ToSrgb1(c.g),ToSrgb1(c.b));}

// Nearest emulated sample given floating point position and texel offset.
// Also zero's off screen.
float3 Fetch(float2 pos, float2 off, texture2d<float> tex, sampler samp){
    pos=(floor(pos*res+off)+ float2(0.5,0.5))/res;
    return ToLinear(1.2 * tex.sample(samp,pos.xy).rgb);}

// Distance in emulated pixels to nearest texel.
float2 Dist(float2 pos){pos=pos*res;return -((pos-floor(pos))-float2(0.5));}

// 1D Gaussian.
float Gaus(float pos,float scale){return exp2(scale*pos*pos);}

// 3-tap Gaussian filter along horz line.
float3 Horz3(float2 pos,float off,texture2d<float> tex, sampler samp){
    float3 b=Fetch(pos, float2(-1.0,off), tex, samp);
    float3 c=Fetch(pos, float2( 0.0,off), tex, samp);
    float3 d=Fetch(pos, float2( 1.0,off), tex, samp);
    float dst=Dist(pos).x;
    // Convert distance to weight.
    float scale=hardPix;
    float wb=Gaus(dst-1.0,scale);
    float wc=Gaus(dst+0.0,scale);
    float wd=Gaus(dst+1.0,scale);
    // Return filtered sample.
    return (b*wb+c*wc+d*wd)/(wb+wc+wd);}

// 5-tap Gaussian filter along horz line.
float3 Horz5(float2 pos,float off, texture2d<float> tex, sampler samp) {
    float3 a=Fetch(pos, float2(-2.0,off), tex, samp);
    float3 b=Fetch(pos, float2(-1.0,off), tex, samp);
    float3 c=Fetch(pos, float2( 0.0,off), tex, samp);
    float3 d=Fetch(pos, float2( 1.0,off), tex, samp);
    float3 e=Fetch(pos, float2( 2.0,off), tex, samp);
    float dst=Dist(pos).x;
    // Convert distance to weight.
    float scale=hardPix;
    float wa=Gaus(dst-2.0,scale);
    float wb=Gaus(dst-1.0,scale);
    float wc=Gaus(dst+0.0,scale);
    float wd=Gaus(dst+1.0,scale);
    float we=Gaus(dst+2.0,scale);
    // Return filtered sample.
    return (a*wa+b*wb+c*wc+d*wd+e*we)/(wa+wb+wc+wd+we);}

// Return scanline weight.
float Scan(float2 pos,float off){
    float dst=Dist(pos).y;
    return Gaus(dst+off,hardScan);}

// Allow nearest three lines to effect pixel.
float3 Tri(float2 pos, texture2d<float> tex, sampler samp) {
    float3 a=Horz3(pos,-1.0, tex, samp);
    float3 b=Horz5(pos, 0.0, tex, samp);
    float3 c=Horz3(pos, 1.0, tex, samp);
    float wa=Scan(pos,-1.0);
    float wb=Scan(pos, 0.0);
    float wc=Scan(pos, 1.0);
    return a*wa+b*wb+c*wc;}

// Distortion of scanlines, and end of screen alpha.
float2 Warp(float2 pos){
    pos=pos*2.0-1.0;
    pos*=float2(1.0+(pos.y*pos.y)*warp.x,1.0+(pos.x*pos.x)*warp.y);
    return pos*0.5+0.5;}

// Shadow mask.
float3 Mask(float2 pos){
    pos.x+=pos.y*3.0;
    float3 mask= float3(maskDark,maskDark,maskDark);
    pos.x=fract(pos.x/6.0);
    if(pos.x<0.333)mask.r=maskLight;
    else if(pos.x<0.666)mask.g=maskLight;
    else mask.b=maskLight;
    return mask;}

struct VertexOutput {
    float4 position [[position]];
    float2 texcoord [[user(texcoord)]];
    float4 color [[user(color)]];
};

fragment float4 Fragment(
    VertexOutput in [[stage_in]],
    constant Params& params [[buffer(0)]],
    texture2d<float> tex [[texture(0)]],
    sampler samp [[sampler(0)]]
) {
    float2 fragCoord = in.position.xy;
    float2 start = fragCoord - float2(params.trg_x, params.trg_y);

    float2 pos=Warp(start/float2(params.trg_w, params.trg_h));

    float3 outColor =Tri(pos, tex, samp)*Mask(fragCoord);
    outColor = ToSrgb(outColor);

    return float4(outColor, 1.0);
}
