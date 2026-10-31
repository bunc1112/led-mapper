#pragma once
// PUMP - shader. Every target is scaled by the beat envelope "Env" (0..1).

namespace pump
{
static const char vertexShader[] = R"(#version 410 core
layout( location = 0 ) in vec4 vPosition;
layout( location = 1 ) in vec2 vUV;
out vec2 uv;
void main()
{
	gl_Position = vPosition;
	uv          = vUV;
}
)";

static const char fragmentShader[] = R"(#version 410 core
uniform sampler2D InputTexture;
uniform vec2  MaxUV;
uniform vec2  Resolution;
uniform float Env;         // envelope 0..1
uniform float SinceHit;    // seconds since last hit
uniform vec2  Shake;       // random direction per hit
uniform float Brightness, Glow, Zoom, Opacity, Saturation, Split, ShakeAmt, Defocus, HueShift, Shock;
uniform int   KeyMode;     // 0 off, 1 luma key, 2 screen (black = transparent)
uniform float KeyLevel, KeySoft, KeyPump;
in vec2 uv;
out vec4 fragColor;

const float TAU = 6.28318530718;
float luma( vec3 c ) { return dot( c, vec3( 0.299, 0.587, 0.114 ) ); }
vec4 src( vec2 p )
{
	vec4 c = texture( InputTexture, clamp( p, 0.0, 1.0 ) * MaxUV );
	return vec4( c.a > 0.0 ? c.rgb / c.a : c.rgb, c.a ); // straight colour
}
vec3 hueRotate( vec3 c, float a )
{
	vec3 k = vec3( 0.57735 );
	return c * cos( a ) + cross( k, c ) * sin( a ) + k * dot( k, c ) * ( 1.0 - cos( a ) );
}

void main()
{
	float aspect = Resolution.x / Resolution.y;
	vec2 c       = uv - 0.5;

	// zoom punch + shake
	float scale = 1.0 + Zoom * 0.18 * Env;
	vec2 p      = c / scale + 0.5 + Shake * ShakeAmt * 0.025 * Env;

	// shockwave ring travelling out from the centre after each hit
	if( Shock > 0.0 )
	{
		vec2 ca    = c * vec2( aspect, 1.0 );
		float d    = length( ca );
		float r    = SinceHit * 1.6;
		float ring = exp( -pow( ( d - r ) * 14.0, 2.0 ) ) * exp( -SinceHit * 2.5 );
		p         -= ( ca / max( d, 1e-4 ) ) * vec2( 1.0 / aspect, 1.0 ) * ring * Shock * 0.035;
	}

	// RGB split along the shake direction (or horizontal)
	vec2 sdir = normalize( Shake + vec2( 1e-3, 0.0 ) ) * vec2( 1.0 / aspect, 1.0 ) * Split * 0.015 * Env;
	vec4 g    = src( p );
	vec4 col  = vec4( src( p + sdir ).r, g.g, src( p - sdir ).b, g.a );

	// defocus between hits, sharp on the hit
	float blurR = Defocus * ( 1.0 - Env ) * 0.012;
	vec4 glow   = vec4( 0.0 );
	if( blurR > 0.0005 || Glow > 0.0 )
	{
		vec4 blur  = col;
		float wsum = 1.0;
		float gr   = 0.02 + 0.03 * Env;
		for( int i = 0; i < 12; i++ )
		{
			float a  = float( i ) * TAU / 12.0 + 0.3;
			vec2 o   = vec2( cos( a ) / aspect, sin( a ) );
			if( blurR > 0.0005 ) { blur += src( p + o * blurR * ( 0.5 + 0.5 * float( i % 2 ) ) ); wsum += 1.0; }
			if( Glow > 0.0 )
			{
				vec4 s = src( p + o * gr * ( 0.4 + 0.6 * float( i % 3 ) / 2.0 ) );
				glow  += s * s.a * smoothstep( 0.35, 0.9, luma( s.rgb ) );
			}
		}
		col   = blur / wsum;
		glow /= 12.0;
	}

	// colour targets
	col.rgb *= 1.0 + Brightness * 1.6 * Env;
	col.rgb  = mix( vec3( luma( col.rgb ) ), col.rgb, 1.0 + Saturation * Env * 1.2 );
	if( HueShift > 0.0 ) col.rgb = hueRotate( col.rgb, HueShift * Env * TAU * 0.5 );
	col.rgb += glow.rgb * Glow * 2.2 * Env;
	col.a    = max( col.a, min( 1.0, luma( glow.rgb ) * Glow * 2.0 * Env ) );
	col.rgb  = max( col.rgb, 0.0 );

	// keying: dark parts become transparent so the layer below is the background
	float k = 1.0;
	float lv = luma( min( col.rgb, vec3( 1.0 ) ) );
	if( KeyMode == 1 )
	{
		float th = max( 0.0, KeyLevel - KeyPump * 0.5 * Env );
		k        = smoothstep( th, th + max( KeySoft, 0.01 ), lv );
	}
	else if( KeyMode == 2 )
	{
		float m = max( col.r, max( col.g, col.b ) );
		k       = clamp( m * ( 1.0 + KeyPump * Env ), 0.0, 1.0 );
		col.rgb = col.rgb / max( k, 1e-4 ); // premultiplied result = original colour -> looks like additive/screen
	}
	float alpha = col.a * k * mix( 1.0 - Opacity, 1.0, Env );

	col.rgb   = min( col.rgb, vec3( 1.0 ) );
	fragColor = vec4( col.rgb * alpha, alpha );
}
)";
}//namespace pump
