#pragma once
// CHRONOS - shaders.
// Kept in their own header so they can also be compiled & previewed outside of Resolume
// (see tools/dump_shaders.cpp + tools/preview_harness.py).

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
// Code shared by the state and main shaders: uniforms, noise, and access to the frame history.
#define CHRONOS_COMMON R"(
uniform sampler2D      InputTexture;
uniform sampler2DArray History;
uniform sampler2D      State;    // feedback buffer for Datamosh / Reaction-Diffusion / Infinite Feedback
uniform vec2  MaxUV;
uniform vec2  Resolution;
uniform vec2  StateRes;
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
uniform float FrameCount;
uniform float StutterAge;// computed on the CPU, beat synced
uniform float Slice;     // increments every beat division
uniform float SliceAge;  // frames since the current beat division started
uniform float BeatPhase; // 0..1 inside the current beat division
uniform float Refresh;   // 1 on the frame a Datamosh "I-frame" should happen

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
vec4 current( vec2 p ) { return texture( InputTexture, clamp( p, 0.0, 1.0 ) * MaxUV ); }
)"

// ------------------------------------------------------------------------------------------------
// Feedback state update (runs before the main pass, at buffer resolution, ping-ponged).
static const char stateShader[] = "#version 410 core\n" CHRONOS_COMMON R"(
// ---------------------------------------------------------------- 8 : DATAMOSH
// Lucas-Kanade optical flow per macroblock between the newest two frames, then the previous
// moshed image is dragged along that flow (the "P-frames"). An I-frame resets it to the live video.
vec4 datamosh()
{
	vec4 now = current( uv );
	if( Refresh > 0.5 || Valid < 3 ) return now;

	float blk  = 16.0;
	vec2 texel = 1.0 / StateRes;
	vec2 cell  = ( floor( uv * StateRes / blk ) + 0.5 ) * blk * texel;
	vec2 d     = texel * 3.0;
	float sxx = 0.0, sxy = 0.0, syy = 0.0, sxt = 0.0, syt = 0.0;
	for( int y = -2; y <= 2; y++ )
	for( int x = -2; x <= 2; x++ )
	{
		vec2 p   = cell + vec2( x, y ) * blk * texel * 0.25;
		float ix = luma( current( p + vec2( d.x, 0 ) ).rgb ) - luma( current( p - vec2( d.x, 0 ) ).rgb );
		float iy = luma( current( p + vec2( 0, d.y ) ).rgb ) - luma( current( p - vec2( 0, d.y ) ).rgb );
		float it = luma( current( p ).rgb ) - luma( pastLayer( p, 1 ).rgb );
		sxx += ix * ix; sxy += ix * iy; syy += iy * iy; sxt += ix * it; syt += iy * it;
	}
	float det = sxx * syy - sxy * sxy + 1e-5;
	vec2 flow = vec2( -( syy * sxt - sxy * syt ), -( sxx * syt - sxy * sxt ) ) / det; // in units of 2*d
	flow     *= 2.0 * d;
	float len = length( flow / texel );
	if( len > 24.0 ) flow *= 24.0 / len;
	if( Reverse > 0.5 ) flow = -flow;

	vec4 moshed = texture( State, uv - flow * ( 0.5 + ParamA * 3.0 ) );
	return mix( moshed, now, 0.004 ); // a drip of fresh pixels keeps it from going fully static
}

// ---------------------------------------------------------------- 10 : REACTION-DIFFUSION
// Gray-Scott, seeded and steered by the video's brightness. R = chemical U, G = chemical V.
vec4 reactionDiffusion()
{
	vec2 t   = 1.0 / StateRes;
	vec4 c   = texture( State, uv );
	if( Valid < 2 || c.a < 0.5 ) return vec4( 1.0, 0.0, 0.0, 1.0 );
	vec2 lap = -c.rg
	         + 0.2  * ( texture( State, uv + vec2( t.x, 0 ) ).rg + texture( State, uv - vec2( t.x, 0 ) ).rg
	                  + texture( State, uv + vec2( 0, t.y ) ).rg + texture( State, uv - vec2( 0, t.y ) ).rg )
	         + 0.05 * ( texture( State, uv + t ).rg + texture( State, uv - t ).rg
	                  + texture( State, uv + vec2( t.x, -t.y ) ).rg + texture( State, uv + vec2( -t.x, t.y ) ).rg );
	// pattern presets: coral -> mitosis -> worms
	vec2 fk = ParamA < 0.5 ? mix( vec2( 0.0545, 0.062 ), vec2( 0.0367, 0.0649 ), ParamA * 2.0 )
	                       : mix( vec2( 0.0367, 0.0649 ), vec2( 0.078, 0.061 ), ParamA * 2.0 - 1.0 );
	float L  = luma( current( uv ).rgb );
	fk.x    += ( L - 0.5 ) * 0.012 * ParamB;
	float u  = c.r, v = c.g;
	float uvv = u * v * v;
	u += 1.0 * lap.x - uvv + fk.x * ( 1.0 - u );
	v += 0.5 * lap.y + uvv - ( fk.x + fk.y ) * v;
	v += step( 0.6, L ) * 0.02 * ParamB + step( 0.995, hash12( uv * 913.0 + Time ) ) * 0.3 * ( 1.0 - ParamB );
	return vec4( clamp( u, 0.0, 1.0 ), clamp( v, 0.0, 1.0 ), 0.0, 1.0 );
}

// ---------------------------------------------------------------- 11 : INFINITE FEEDBACK
vec4 infiniteFeedback()
{
	vec4 now  = current( uv );
	float z   = ( ParamA - 0.5 ) * 0.06;
	float r   = ( ParamA - 0.5 ) * ( Reverse > 0.5 ? -0.08 : 0.08 );
	vec2 c    = uv - 0.5;
	c.x      *= Resolution.x / Resolution.y;
	c         = mat2( cos( r ), -sin( r ), sin( r ), cos( r ) ) * c / ( 1.0 + z );
	c.x      /= Resolution.x / Resolution.y;
	vec4 fb   = texture( State, c + 0.5 );
	float dec = mix( 0.82, 0.992, ParamB );
	// slow hue drift so the tail turns into a rainbow
	float a   = 0.06;
	vec3 k    = vec3( 0.57735 );
	vec3 rgb  = fb.rgb * cos( a ) + cross( k, fb.rgb ) * sin( a ) + k * dot( k, fb.rgb ) * ( 1.0 - cos( a ) );
	vec4 tail = vec4( clamp( rgb, 0.0, 1.0 ), fb.a ) * dec;
	return max( now, tail );
}

void main()
{
	if( Mode == 8 )       fragColor = datamosh();
	else if( Mode == 10 ) fragColor = reactionDiffusion();
	else                  fragColor = infiniteFeedback();
}
)";

// ------------------------------------------------------------------------------------------------
// The main time machine. Every mode reads from a ring buffer of past frames.
static const char mainShader[] = "#version 410 core\n" CHRONOS_COMMON R"(
// ---------------------------------------------------------------- 0 : SLIT-SCAN
vec4 slitScan( vec4 now )
{
	vec2 c       = uv - 0.5;
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
	vec2 dir = vec2( cos( ParamB * TAU ), sin( ParamB * TAU ) ) * ParamA * 0.04;
	float aR = Reverse > 0.5 ? maxAge() : 0.0;
	float aB = Reverse > 0.5 ? 0.0 : maxAge();
	vec4 r   = past( uv + dir, aR );
	vec4 g   = past( uv, maxAge() * 0.5 );
	vec4 b   = past( uv - dir, aB );
	return vec4( r.r, g.g, b.b, max( max( r.a, g.a ), b.a ) );
}

// ---------------------------------------------------------------- 2 : BEAT STUTTER
vec4 beatStutter( vec4 now )
{
	return StutterAge < 0.5 ? now : past( uv, StutterAge );
}

// ---------------------------------------------------------------- 3 : CHRONO TRAILS
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
vec4 ruttEtra( vec4 now )
{
	float L     = floor( mix( 24.0, 220.0, ParamB ) );
	float H     = ParamA * 0.3;
	float py    = uv.y;
	int j0      = int( floor( py * L - 0.5 ) );
	int span    = min( int( ceil( H * L ) ) + 2, 96 );
	vec3 col    = vec3( 0.0 );
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
vec4 timeMosaic( vec4 now )
{
	float bs   = mix( 8.0, 180.0, ParamA * ParamA );
	vec2 cell  = floor( uv * Resolution / bs );
	float s    = mod( Slice, 997.0 );
	float pick = hash12( cell + s * 17.13 );
	if( pick > ParamB ) return now;
	float age  = floor( hash12( cell * 1.7 + s * 3.1 + 5.0 ) * maxAge() );
	vec4 f     = past( uv, age );
	if( Reverse > 0.5 ) f.rgb = f.a - f.rgb;
	return f;
}

// ---------------------------------------------------------------- 6 : LONG EXPOSURE
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
		vec4 f     = past( p, fk * maxAge() );
		float a    = k == K - 1 ? 1.0 : smoothstep( ParamB, ParamB + 0.08, luma( f.rgb ) ) * f.a;
		float fade = 1.0 - fk * 0.55;
		acc += ( 1.0 - acc.a ) * vec4( f.rgb * fade * ( a / max( f.a, 1e-4 ) ), a );
		if( acc.a > 0.995 ) break;
	}
	return acc;
}

// ---------------------------------------------------------------- 9 : PIXEL MELT
// Pixel-sort style drip: bright pixels run down into darker areas (sideways with Reverse),
// and the further a drip falls the further back in time it was sampled.
vec4 pixelMelt( vec4 now )
{
	float th  = mix( 0.92, 0.08, ParamA );
	vec2 dir  = Reverse > 0.5 ? vec2( -1.0, 0.0 ) : vec2( 0.0, 1.0 );
	float len = mix( 12.0, Resolution.y * 0.9, ParamB * ParamB );
	vec2 st   = dir * ( len / 48.0 ) / Resolution;
	vec4 c    = now;
	float best = max( luma( now.rgb ), th );
	for( int i = 1; i <= 48; i++ )
	{
		vec2 p = uv + st * float( i );
		if( p.x < 0.0 || p.y > 1.0 ) break;
		float fi = float( i ) / 48.0;
		vec4 s   = past( p, fi * maxAge() );
		float l  = luma( s.rgb ) * ( 1.0 - fi * 0.35 ); // drips get dimmer as they fall
		if( l > best ) { best = l; c = s * ( 1.0 - fi * 0.35 ); }
	}
	return c;
}

// ---------------------------------------------------------------- 12 : VHS REWIND
vec4 vhsRewind( vec4 now )
{
	float dmg   = ParamB;
	float span  = max( maxAge(), 2.0 );
	float rate  = 2.0 + ParamA * 3.0;                 // 1x .. 4x backwards
	float age   = Reverse > 0.5 ? 0.0 : mod( FrameCount * ( rate - 1.0 ), span );
	float row   = floor( uv.y * Resolution.y * 0.5 );
	float jit   = ( hash12( vec2( row, floor( Time * 30.0 ) ) ) - 0.5 ) * 0.006 * dmg
	            + sin( uv.y * 40.0 + Time * 12.0 ) * 0.0025 * dmg;
	float band  = fract( Time * 0.23 + ( Reverse > 0.5 ? 0.0 : age / span * 0.5 ) );
	float inBand = smoothstep( 0.05, 0.0, abs( uv.y - band ) );
	jit        += inBand * ( hash12( vec2( row, Time ) ) - 0.5 ) * 0.08 * ( 0.3 + dmg );
	vec2 p      = vec2( uv.x + jit, uv.y );
	float ca    = 0.002 + 0.006 * dmg;
	vec4 g      = past( p, age );
	vec3 col    = vec3( past( p + vec2( ca, 0.0 ), age ).r, g.g, past( p - vec2( ca, 0.0 ), age ).b );
	float y     = luma( col );
	col         = mix( vec3( y ), col, 1.25 );           // tape saturation
	col        *= 0.88 + 0.12 * sin( uv.y * Resolution.y * 1.5708 );
	col        += ( hash12( uv * Resolution + Time * 60.0 ) - 0.5 ) * ( 0.06 + 0.25 * inBand ) * dmg;
	col        += inBand * 0.15 * dmg;
	return vec4( clamp( col, 0.0, 1.0 ), max( g.a, inBand ) );
}

// ---------------------------------------------------------------- 13 : VIDEO SCRATCH
// The playhead is scratched back and forth on the beat, like a DJ on a turntable.
vec4 videoScratch( vec4 now )
{
	float ph  = ParamB > 0.5 ? fract( BeatPhase * 2.0 ) : BeatPhase;
	float s   = sin( 3.14159265 * ph );
	s         = pow( s, mix( 0.35, 3.0, ParamA ) );
	if( Reverse > 0.5 ) s = 1.0 - s;
	return past( uv, s * maxAge() );
}

// ---------------------------------------------------------------- 14 : BEAT RIPPLE
// On every beat a wave travels outward. Ahead of the wave you see the past (Depth ago);
// behind it, time snaps back to live.
vec4 beatRipple( vec4 now )
{
	float aspect = Resolution.x / Resolution.y;
	vec2 rnd     = vec2( hash12( vec2( Slice, 1.3 ) ), hash12( vec2( Slice, 7.7 ) ) ) * 0.8 + 0.1;
	vec2 center  = mix( vec2( 0.5 ), rnd, ParamB );
	vec2 c       = uv - center;
	c.x         *= aspect;
	float d      = length( c );
	float R      = BeatPhase * ( 1.2 + aspect * 0.5 );
	float front  = d - R;
	float wave   = exp( -front * front * 300.0 ) * ParamA;
	vec2 disp    = ( c / max( d, 1e-4 ) ) * wave * 0.035;
	disp.x      /= aspect;
	bool ahead   = Reverse > 0.5 ? front < 0.0 : front > 0.0;
	vec4 col     = past( uv - disp, ahead ? max( maxAge(), SliceAge ) : 0.0 );
	col.rgb     += wave * 0.25 * col.a;
	return col;
}

// ---------------------------------------------------------------- 15 : SLICE SHIFT
vec4 sliceShift( vec4 now )
{
	float bands = floor( mix( 4.0, 48.0, ParamA ) );
	float b     = floor( uv.y * bands );
	float s     = mod( Slice, 997.0 );
	if( hash12( vec2( b, s ) ) > ParamB ) return now;
	float shift = ( hash12( vec2( b * 3.1, s + 11.0 ) ) - 0.5 ) * 0.35;
	float age   = floor( hash12( vec2( b * 7.7, s + 5.0 ) ) * maxAge() );
	vec2 p      = vec2( fract( uv.x + shift ), uv.y );
	vec4 g      = past( p, age );
	float ca    = 0.01 * hash12( vec2( b, s * 2.0 ) );
	vec4 col    = vec4( past( p + vec2( ca, 0 ), age ).r, g.g, past( p - vec2( ca, 0 ), age ).b, g.a );
	if( Reverse > 0.5 ) col.rgb = col.a - col.rgb;
	return col;
}

// ---------------------------------------------------------------- 16 : STROBE ECHO
// Stroboscopic multiple exposure: a few crisp copies of the past instead of a blur.
vec4 strobeEcho( vec4 now )
{
	int n    = 2 + int( floor( ParamA * 6.99 ) );
	vec4 acc = vec4( 0.0 );
	for( int k = 0; k < 8; k++ )
	{
		if( k >= n ) break;
		float fk = float( k ) / float( n - 1 );
		float w  = pow( 1.0 - fk * 0.85, ParamB * 3.0 );
		if( Reverse > 0.5 ) w = pow( 0.15 + fk * 0.85, ParamB * 3.0 );
		acc = max( acc, past( uv, fk * maxAge() ) * w );
	}
	return acc;
}

// ---------------------------------------------------------------- 17 : NEON ECHO
// Edges of the last moments, each age glowing in its own colour.
vec4 neonEcho( vec4 now )
{
	const int K = 8;
	vec2 px  = 1.5 / Resolution;
	float th = mix( 0.5, 0.04, ParamA );
	vec3 acc = vec3( 0.0 );
	for( int k = 0; k < K; k++ )
	{
		float fk  = float( k ) / float( K - 1 );
		float age = fk * maxAge();
		float lx  = luma( past( uv + vec2( px.x, 0 ), age ).rgb ) - luma( past( uv - vec2( px.x, 0 ), age ).rgb );
		float ly  = luma( past( uv + vec2( 0, px.y ), age ).rgb ) - luma( past( uv - vec2( 0, px.y ), age ).rgb );
		float e   = smoothstep( th, th + 0.15, length( vec2( lx, ly ) ) * 2.0 );
		float h   = ( Reverse > 0.5 ? 1.0 - fk : fk ) * 0.7 + Time * 0.07;
		acc += hue( h ) * e * ( 1.0 - fk * 0.75 );
	}
	vec3 col = min( now.rgb * ParamB * 0.7 + acc, vec3( 1.0 ) );
	return vec4( col, max( now.a * ParamB, max( col.r, max( col.g, col.b ) ) ) );
}

// ---------------------------------------------------------------- 18 : TIME KALEIDO
// Kaleidoscope where every mirrored segment shows a different moment.
vec4 timeKaleido( vec4 now )
{
	float aspect = Resolution.x / Resolution.y;
	float n      = floor( mix( 3.0, 16.0, ParamA ) );
	vec2 c       = uv - 0.5;
	c.x         *= aspect;
	float r      = length( c );
	float a      = mod( atan( c.y, c.x ) + Time * ( ParamB - 0.5 ) * 1.5, TAU );
	float seg    = TAU / n;
	float si     = floor( a / seg );
	float la     = mod( a, seg );
	if( mod( si, 2.0 ) > 0.5 ) la = seg - la;
	vec2 p       = vec2( cos( la ), sin( la ) ) * r;
	p.x         /= aspect;
	p           += 0.5;
	p            = 1.0 - abs( 1.0 - mod( p, 2.0 ) );
	float t      = Reverse > 0.5 ? clamp( r * 1.6, 0.0, 1.0 ) : si / max( n - 1.0, 1.0 );
	return past( p, t * maxAge() );
}

// ---------------------------------------------------------------- 19 : MOTION KEY / CLOAK
// Param B = 0: only what moves stays visible (alpha keyed). Param B = 1: what moves turns
// into a refractive "invisibility cloak" that shows the background from Depth ago.
vec4 motionKey( vec4 now )
{
	vec2 px  = 3.0 / Resolution;
	float th = mix( 0.35, 0.02, ParamA );
	float m  = 0.0;
	m += length( now.rgb - past( uv, 2.0 ).rgb );
	m += length( current( uv + vec2( px.x, 0 ) ).rgb - past( uv + vec2( px.x, 0 ), 2.0 ).rgb );
	m += length( current( uv - vec2( px.x, 0 ) ).rgb - past( uv - vec2( px.x, 0 ), 2.0 ).rgb );
	m += length( current( uv + vec2( 0, px.y ) ).rgb - past( uv + vec2( 0, px.y ), 2.0 ).rgb );
	m += length( current( uv - vec2( 0, px.y ) ).rgb - past( uv - vec2( 0, px.y ), 2.0 ).rgb );
	m  = smoothstep( th, th + 0.12, m / 5.0 );
	if( Reverse > 0.5 ) m = 1.0 - m;
	vec4 keyed = now * m;
	vec2 g     = vec2( luma( current( uv + vec2( px.x, 0 ) ).rgb ) - luma( current( uv - vec2( px.x, 0 ) ).rgb ),
	                   luma( current( uv + vec2( 0, px.y ) ).rgb ) - luma( current( uv - vec2( 0, px.y ) ).rgb ) );
	vec4 bg    = past( uv + g * 0.03 * m, max( maxAge(), 2.0 ) );
	vec4 cloak = mix( now, bg + vec4( vec3( 0.08 ) * m * bg.a, 0.0 ), m );
	return mix( keyed, cloak, ParamB );
}

void main()
{
	vec4 now = current( uv );
	vec4 col;
	if( Mode == 0 )       col = slitScan( now );
	else if( Mode == 1 )  col = chromaSplit( now );
	else if( Mode == 2 )  col = beatStutter( now );
	else if( Mode == 3 )  col = chronoTrails( now );
	else if( Mode == 4 )  col = ruttEtra( now );
	else if( Mode == 5 )  col = timeMosaic( now );
	else if( Mode == 6 )  col = longExposure( now );
	else if( Mode == 7 )  col = timeTunnel( now );
	else if( Mode == 8 )  col = texture( State, uv );
	else if( Mode == 9 )  col = pixelMelt( now );
	else if( Mode == 10 )
	{
		float v   = texture( State, uv ).g;
		float pat = smoothstep( 0.12, 0.3, v );
		if( Reverse > 0.5 ) pat = 1.0 - pat;
		vec3 vid  = now.a > 0.0 ? now.rgb / now.a : now.rgb;
		vec3 ink  = mix( vec3( 0.03 ), vid * 1.3 + 0.08, pat );
		col       = vec4( ink + smoothstep( 0.25, 0.32, v ) * 0.15, 1.0 );
	}
	else if( Mode == 11 ) col = texture( State, uv );
	else if( Mode == 12 ) col = vhsRewind( now );
	else if( Mode == 13 ) col = videoScratch( now );
	else if( Mode == 14 ) col = beatRipple( now );
	else if( Mode == 15 ) col = sliceShift( now );
	else if( Mode == 16 ) col = strobeEcho( now );
	else if( Mode == 17 ) col = neonEcho( now );
	else if( Mode == 18 ) col = timeKaleido( now );
	else                  col = motionKey( now );

	col       = mix( now, col, Mix );
	// Output must stay premultiplied and inside LDR.
	col.a     = clamp( col.a, 0.0, 1.0 );
	col.rgb   = clamp( col.rgb, vec3( 0.0 ), vec3( col.a ) );
	fragColor = col;
}
)";

}//namespace chronos
