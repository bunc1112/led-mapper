#pragma once
// CUTUP - shaders. One pass draws every primitive (cards, circles, SDF letters) produced by the engine.

namespace cutup
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
uniform sampler2D Atlas;      // SDF font atlas (R8)
uniform sampler2D Prims;      // 4 x N RGBA32F: one row per primitive
uniform vec2  MaxUV;
uniform vec2  Resolution;
uniform vec2  AtlasSize;
uniform int   Count;
uniform vec3  TextColor;
uniform int   Output;         // 0 video in shapes, 1 holes (layer below shows through), 2 matte
uniform int   Background;     // 0 transparent, 1 black, 2 colour vignette, 3 dimmed video, 4 video
in vec2 uv;
out vec4 fragColor;

vec4 video( vec2 p ) { return texture( InputTexture, clamp( p, 0.0, 1.0 ) * MaxUV ); }
float luma( vec3 c ) { return dot( c, vec3( 0.299, 0.587, 0.114 ) ); }
vec3 solid( int c )
{
	if( c == 0 ) return TextColor;
	if( c == 1 ) return vec3( 0.93, 0.89, 0.80 ); // cream, like the flash cards in classic recap edits
	if( c == 2 ) return vec3( 1.0 );
	return vec3( 0.0 );
}
float sdBox( vec2 p, vec2 b, float r )
{
	vec2 q = abs( p ) - b + r;
	return length( max( q, 0.0 ) ) + min( max( q.x, q.y ), 0.0 ) - r;
}

void main()
{
	float aspect = Resolution.x / Resolution.y;
	vec2 P       = vec2( uv.x * aspect, uv.y );
	float px     = 1.0 / Resolution.y;
	vec4 acc     = vec4( 0.0 );

	for( int i = 0; i < Count; i++ )
	{
		vec4 A = texelFetch( Prims, ivec2( 0, i ), 0 ); // cx cy hw hh
		vec4 B = texelFetch( Prims, ivec2( 1, i ), 0 ); // rot alpha type fill
		vec2 d = P - A.xy;
		if( B.x != 0.0 ) d = mat2( cos( B.x ), -sin( B.x ), sin( B.x ), cos( B.x ) ) * d;
		vec2 ext = A.zw;
		if( any( greaterThan( abs( d ), ext + 0.07 ) ) || B.y <= 0.0 ) continue;
		vec4 C   = texelFetch( Prims, ivec2( 2, i ), 0 );
		vec4 D   = texelFetch( Prims, ivec2( 3, i ), 0 ); // shadow tint radius color
		int type = int( B.z + 0.5 );
		int fill = int( B.w + 0.5 );

		// soft drop shadow under cards
		if( type != 2 && D.x > 0.0 )
		{
			float sd = type == 0 ? sdBox( d - vec2( 0.008, -0.014 ), ext, D.z ) : length( d - vec2( 0.008, -0.014 ) ) - ext.x;
			float sh = ( 1.0 - smoothstep( -0.01, 0.045, sd ) ) * D.x * 0.65 * B.y;
			acc      = vec4( 0.0, 0.0, 0.0, sh ) + acc * ( 1.0 - sh );
		}

		float cov = 0.0;
		vec2 l    = d / max( ext, vec2( 1e-5 ) ) * 0.5 + 0.5; // 0..1 inside the primitive
		if( type == 0 )      cov = clamp( 0.5 - sdBox( d, ext, D.z ) / px, 0.0, 1.0 );
		else if( type == 1 ) cov = clamp( 0.5 - ( length( d ) - ext.x ) / px, 0.0, 1.0 );
		else if( all( greaterThanEqual( l, vec2( 0.0 ) ) ) && all( lessThanEqual( l, vec2( 1.0 ) ) ) )
		{
			vec2 auv    = vec2( mix( C.x, C.z, l.x ), mix( C.w, C.y, l.y ) );
			float sdf   = texture( Atlas, auv ).r;
			float texPx = ( C.w - C.y ) * AtlasSize.y / max( 2.0 * ext.y / px, 1e-4 ); // atlas texels per screen pixel
			float dpx   = max( texPx / 12.0, 1e-4 );                                     // sdf change per screen pixel
			cov         = clamp( ( sdf - 0.5 ) / dpx + 0.5, 0.0, 1.0 );
		}
		if( cov <= 0.0 ) continue;

		vec3 col;
		if( fill == 0 )
		{
			vec3 v      = video( uv ).rgb;
			vec3 tinted = solid( int( D.w + 0.5 ) ) * ( 0.6 + 1.3 * luma( v ) ) + v * 0.15;
			col         = mix( v, tinted, D.y );
		}
		else if( fill == 1 )
		{
			vec2 cuv = ( l - 0.5 ) / max( C.z, 0.05 ) + 0.5 + C.xy;
			col      = video( cuv ).rgb;
			col     *= mix( 0.45, 1.0, 1.0 - smoothstep( 0.55, 1.5, length( ( l - 0.5 ) * 2.0 ) ) ); // card vignette
			col     *= 1.0 - C.w;
		}
		else if( fill == 2 )
			col = solid( int( D.w + 0.5 ) );
		else
		{
			float g = 1.0 - smoothstep( 0.0, 0.95, length( l - C.xy ) );
			col     = mix( vec3( 0.95, 0.22, 0.04 ), vec3( 1.0, 0.86, 0.38 ), g ) * ( 0.55 + 0.6 * g );
		}
		float a = cov * B.y;
		acc     = vec4( col * a, a ) + acc * ( 1.0 - a );
	}

	vec4 inp = video( uv );
	vec4 bg  = vec4( 0.0 );
	if( Background == 1 ) bg = vec4( 0.0, 0.0, 0.0, 1.0 );
	else if( Background == 2 )
	{
		float r = length( ( uv - 0.5 ) * vec2( aspect, 1.0 ) );
		bg      = vec4( TextColor * mix( 1.0, 0.22, smoothstep( 0.1, 0.95, r ) ), 1.0 );
	}
	else if( Background == 3 ) bg = vec4( inp.rgb * 0.28, inp.a );
	else if( Background == 4 ) bg = inp;

	if( Output == 1 )
	{
		vec4 outside = Background == 0 ? inp : bg;
		fragColor    = outside * ( 1.0 - acc.a );
	}
	else if( Output == 2 )
		fragColor = vec4( vec3( acc.a ), acc.a );
	else
		fragColor = acc + bg * ( 1.0 - acc.a );
	fragColor = clamp( fragColor, 0.0, 1.0 );
	fragColor.rgb = min( fragColor.rgb, vec3( fragColor.a ) );
}
)";
}//namespace cutup
