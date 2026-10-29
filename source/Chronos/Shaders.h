#pragma once
// CHRONOS - shaders.
// Kept in their own header so they can also be compiled & previewed outside of Resolume.

namespace chronos
{
// ------------------------------------------------------------------------------------------------
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

// ------------------------------------------------------------------------------------------------
// Copies the current input frame (its content area) into one layer of the history texture array.
static const char copyShader[] = R"(#version 410 core
uniform sampler2D InputTexture;
uniform vec2 MaxUV;
in vec2 uv;
out vec4 fragColor;
void main()
{
	fragColor = texture( InputTexture, uv * MaxUV );
}
)";

// ------------------------------------------------------------------------------------------------
// The main time machine. Every mode reads from a ring buffer of past frames.
static const char mainShader[] = R"(#version 410 core
uniform sampler2D      InputTexture;
uniform sampler2DArray History;
uniform vec2  MaxUV;
uniform vec2  Resolution;
uniform int   Mode;
uniform int   TimeMap;
uniform int   Head;      // layer holding the newest frame
uniform int   Frames;    // number of layers in the ring buffer
uniform int   Valid;     // how many layers have been written so far
uniform float Depth;     // 0..1 : how far back in the buffer we may look
uniform float ParamA;
uniform float ParamB;
uniform float Reverse;
uniform float Mix;
uniform float Time;
uniform float StutterAge;// computed on the CPU, beat synced
uniform float Slice;     // increments every beat division

in vec2 uv;
out vec4 fragColor;

const float TAU = 6.28318530718;

float hash12( vec2 p )
{
	vec3 p3 = fract( vec3( p.xyx ) * 0.1031 );
	p3 += dot( p3, p3.yzx + 33.33 );
	return fract( ( p3.x + p3.y ) * p3.z );
}
float vnoise( vec2 p )
{
	vec2 i = floor( p ), f = fract( p );
	vec2 u = f * f * ( 3.0 - 2.0 * f );
	return mix( mix( hash12( i ), hash12( i + vec2( 1, 0 ) ), u.x ),
	            mix( hash12( i + vec2( 0, 1 ) ), hash12( i + vec2( 1, 1 ) ), u.x ), u.y );
}
float fbm( vec2 p )
{
	float v = 0.0, a = 0.5;
	for( int i = 0; i < 4; i++ ) { v += a * vnoise( p ); p *= 2.03; a *= 0.5; }
	return v / 0.9375;
}
float luma( vec3 c ) { return dot( c, vec3( 0.299, 0.587, 0.114 ) ); }
vec3 hue( float h ) { return clamp( abs( fract( h + vec3( 0.0, 2.0 / 3.0, 1.0 / 3.0 ) ) * 6.0 - 3.0 ) - 1.0, 0.0, 1.0 ); }

float lastAge() { return float( max( Valid - 1, 0 ) ); }
float maxAge()  { return lastAge() * Depth; }

vec4 pastLayer( vec2 p, int age )
{
	age   = clamp( age, 0, max( Valid - 1, 0 ) );
	int l = ( Head - age ) % Frames;
	if( l < 0 ) l += Frames;
	return texture( History, vec3( p, float( l ) ) );
}
// Fractional ages blend two neighbouring frames, which keeps slit-scans smooth instead of banded.
vec4 past( vec2 p, float age )
{
	age      = clamp( age, 0.0, lastAge() );
	float a0 = floor( age );
	return mix( pastLayer( p, int( a0 ) ), pastLayer( p, int( a0 ) + 1 ), age - a0 );
}

// ---------------------------------------------------------------- 0 : SLIT-SCAN
vec4 slitScan( vec4 now )
{
	vec2 c      = uv - 0.5;
	float aspect = Resolution.x / Resolution.y;
	c.x *= aspect;
	float reps = 1.0 + floor( ParamA * 7.99 );
	float m;
	if( TimeMap == 0 )      m = uv.x;
	else if( TimeMap == 1 ) m = 1.0 - uv.y;
	else if( TimeMap == 2 ) m = length( c ) / length( vec2( 0.5 * aspect, 0.5 ) );
	else if( TimeMap == 3 ) m = atan( c.y, c.x ) / TAU + 0.5 + length( c ) * reps * 0.5;
	else if( TimeMap == 4 ) m = fbm( uv * vec2( aspect, 1.0 ) * ( 1.5 + ParamA * 8.0 ) + Time * ParamB * 0.6 );
	else                    m = luma( now.a > 0.0 ? now.rgb / now.a : now.rgb );

	if( TimeMap <= 2 ) m = fract( min( m, 0.9999 ) * reps );
	if( TimeMap <= 3 ) m = fract( m + Time * ParamB * 0.25 );
	if( Reverse > 0.5 ) m = 1.0 - m;
	return past( uv, clamp( m, 0.0, 1.0 ) * maxAge() );
}

// ---------------------------------------------------------------- 1 : CHROMA TIME SPLIT
vec4 chromaSplit( vec4 now )
{
	vec2 dir   = vec2( cos( ParamB * TAU ), sin( ParamB * TAU ) ) * ParamA * 0.04;
	float aR   = Reverse > 0.5 ? maxAge() : 0.0;
	float aB   = Reverse > 0.5 ? 0.0 : maxAge();
	vec4 r     = past( uv + dir, aR );
	vec4 g     = past( uv, maxAge() * 0.5 );
	vec4 b     = past( uv - dir, aB );
	return vec4( r.r, g.g, b.b, max( max( r.a, g.a ), b.a ) );
}

// ---------------------------------------------------------------- 2 : BEAT STUTTER
vec4 beatStutter( vec4 now )
{
	return StutterAge < 0.5 ? now : past( uv, StutterAge );
}

// ---------------------------------------------------------------- 3 : CHRONO TRAILS
// Detects motion between successive past frames and paints it in a colour per age.
vec4 chronoTrails( vec4 now )
{
	const int K   = 10;
	float stepAge = max( maxAge(), float( K ) ) / float( K );
	float th      = mix( 0.45, 0.03, ParamA );
	vec3 acc      = vec3( 0.0 );
	vec4 prev     = now;
	for( int k = 1; k <= K; k++ )
	{
		vec4 f     = past( uv, float( k ) * stepAge );
		float d    = length( prev.rgb - f.rgb );
		float mask = smoothstep( th, th + 0.12, d );
		float fk   = float( k ) / float( K );
		float h    = ( Reverse > 0.5 ? 1.0 - fk : fk ) * 0.8 + Time * 0.05;
		acc += hue( h ) * mask * ( 1.15 - fk );
		prev = f;
	}
	vec3 col = min( now.rgb * ParamB + acc * 0.55, vec3( 1.0 ) );
	return vec4( col, max( now.a * ParamB, max( col.r, max( col.g, col.b ) ) ) );
}

// ---------------------------------------------------------------- 4 : RUTT-ETRA (time-sliced)
// Classic scanline rasterizer: every line is lifted by the brightness underneath it.
// With Depth > 0 each line also comes from a different moment in time.
vec4 ruttEtra( vec4 now )
{
	float L    = floor( mix( 24.0, 220.0, ParamB ) );
	float H    = ParamA * 0.3;
	float py   = uv.y;
	int j0     = int( floor( py * L - 0.5 ) );
	int span   = min( int( ceil( H * L ) ) + 2, 96 );
	vec3 col   = vec3( 0.0 );
	float alpha = 0.0;
	for( int i = -1; i <= span; i++ )
	{
		int j = j0 - i;
		if( j < 0 ) break;
		if( float( j ) >= L ) continue;
		float by  = ( float( j ) + 0.5 ) / L;
		float t   = Reverse > 0.5 ? 1.0 - by : by;
		vec4 s    = past( vec2( uv.x, by ), t * maxAge() );
		float lum = luma( s.rgb );
		float y   = by + lum * H;
		float d   = abs( py - y ) * Resolution.y;
		float w   = 1.0 - smoothstep( 0.4, 1.4, d );
		vec3 c    = mix( vec3( lum ), s.rgb, 0.85 ) * 1.25 + 0.06;
		col       = max( col, c * w );
		alpha     = max( alpha, w );
	}
	return vec4( min( col, vec3( 1.0 ) ), alpha );
}

// ---------------------------------------------------------------- 5 : TIME MOSAIC
// Blocks of the image fall behind in time; reshuffles every beat division.
vec4 timeMosaic( vec4 now )
{
	float bs   = mix( 8.0, 180.0, ParamA * ParamA );
	vec2 cell  = floor( uv * Resolution / bs );
	float s    = mod( Slice, 997.0 );
	float pick = hash12( cell + s * 17.13 );
	if( pick > ParamB ) return now;
	float age  = floor( hash12( cell * 1.7 + s * 3.1 + 5.0 ) * maxAge() );
	vec4 f     = past( uv, age );
	if( Reverse > 0.5 ) f.rgb = f.a - f.rgb; // negative blocks
	return f;
}

// ---------------------------------------------------------------- 6 : LONG EXPOSURE
// Average (photographic shutter) to Max (light painting).
vec4 longExposure( vec4 now )
{
	const int K = 24;
	vec4 sum = vec4( 0.0 ), mx = vec4( 0.0 );
	float ws = 0.0;
	for( int k = 0; k < K; k++ )
	{
		float fk = float( k ) / float( K - 1 );
		float w  = pow( 1.0 - fk * 0.97, ParamB * 5.0 );
		if( Reverse > 0.5 ) w = pow( 0.03 + fk * 0.97, ParamB * 5.0 );
		vec4 f   = past( uv, fk * maxAge() );
		sum += f * w;
		ws  += w;
		mx   = max( mx, f * mix( 1.0, w, 0.6 ) );
	}
	return mix( sum / max( ws, 1e-4 ), mx, ParamA );
}

// ---------------------------------------------------------------- 7 : TIME TUNNEL
// Older frames recede into the distance; bright parts of newer frames sit in front of them.
vec4 timeTunnel( vec4 now )
{
	const int K = 12;
	float z     = 1.0 + ParamA * 0.5;
	vec4 acc    = vec4( 0.0 );
	for( int k = 0; k < K; k++ )
	{
		float fk = float( k ) / float( K - 1 );
		float s  = pow( z, Reverse > 0.5 ? -float( k ) : float( k ) );
		vec2 p   = 0.5 + ( uv - 0.5 ) * s;
		if( any( lessThan( p, vec2( 0.0 ) ) ) || any( greaterThan( p, vec2( 1.0 ) ) ) ) continue;
		vec4 f   = past( p, fk * maxAge() );
		float a  = k == K - 1 ? 1.0 : smoothstep( ParamB, ParamB + 0.08, luma( f.rgb ) ) * f.a;
		float fade = 1.0 - fk * 0.55;
		acc += ( 1.0 - acc.a ) * vec4( f.rgb * fade * ( a / max( f.a, 1e-4 ) ), a );
		if( acc.a > 0.995 ) break;
	}
	return acc;
}

void main()
{
	vec4 now = texture( InputTexture, uv * MaxUV );
	vec4 col;
	if( Mode == 0 )      col = slitScan( now );
	else if( Mode == 1 ) col = chromaSplit( now );
	else if( Mode == 2 ) col = beatStutter( now );
	else if( Mode == 3 ) col = chronoTrails( now );
	else if( Mode == 4 ) col = ruttEtra( now );
	else if( Mode == 5 ) col = timeMosaic( now );
	else if( Mode == 6 ) col = longExposure( now );
	else                 col = timeTunnel( now );

	col       = mix( now, col, Mix );
	// Output must stay premultiplied and inside LDR.
	col.a     = clamp( col.a, 0.0, 1.0 );
	col.rgb   = clamp( col.rgb, vec3( 0.0 ), vec3( col.a ) );
	fragColor = col;
}
)";

}//namespace chronos
