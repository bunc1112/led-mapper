#include "Cutup.h"
#include "Shaders.h"
#include "FontAtlas.h"
#include <algorithm>
#include <cmath>

using namespace ffglex;
using namespace cutup;

enum ParamID : FFUInt32
{
	PT_NEXT,
	PT_AUTO,
	PT_DIVISION,
	PT_SCENE_ORDER,
	PT_OUTPUT,
	PT_BACKGROUND,
	PT_HUE,
	PT_SAT,
	PT_BRI,
	PT_TEXT_ORDER,
	PT_REVEAL,
	PT_FONT,
	PT_TEXT_SIZE,
	PT_TEXT_POS,
	PT_TINT,
	PT_UPPER,
	PT_OVERLAY,
	PT_FLASH,
	PT_SPEED,
	PT_SHADOW,
	PT_SCENE0,                          // kSceneCount booleans
	PT_TEXT0 = PT_SCENE0 + kSceneCount, // kTextCount text boxes
	PT_COUNT = PT_TEXT0 + kTextCount
};

static CFFGLPluginInfo PluginInfo(
	PluginFactory< Cutup >,
	"CUTP",
	"Cutup",
	2, 1,
	1, 0,
	FF_EFFECT,
	"Beat-synced recap montage: words pop in letter by letter with video inside, cards cascade/zoom/scatter, iris, blinds, grid. Next button or auto on the beat.",
	"CUTUP FFGL - fonts: Archivo Black & Anton (SIL OFL)" );

static const char* kDivisionNames[ 7 ] = { "1/4 Beat", "1/2 Beat", "1 Beat", "2 Beats", "1 Bar", "2 Bars", "4 Bars" };
static const float kDivisionBeats[ 7 ] = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f };
static const char* kOrderNames[ 3 ]    = { "Sequential", "Random", "Shuffle" };
static const char* kTextOrderNames[ 4 ] = { "Sequential", "Random", "Shuffle", "Ping-Pong" };
static const char* kRevealNames[ 5 ]   = { "Random Pop", "Left to Right", "Slide Up", "Scramble", "All at Once" };
static const char* kOutputNames[ 3 ]   = { "Video in Shapes", "Holes (Layer Below)", "Matte" };
static const char* kBgNames[ 5 ]       = { "Transparent", "Black", "Colour Vignette", "Dimmed Video", "Video" };
static const char* kPosNames[ 3 ]      = { "Center", "Random", "Top / Mid / Bottom" };
static const char* kOverlayNames[ 2 ]  = { "Off", "Label on Cuts" };
// Default words. UTF-8 escapes so every compiler reads them the same way.
static const char* kDefaultTexts[ kTextCount ] = {
	"2026", "RECAP", "MOMENTS", "NO\xC4\x86 ZA|PAM\xC4\x86" "ENJE", "LIVE", "", "", "", "", "", "", "", "", "", "", ""
};

static void HsbToRgb( float h, float s, float v, float out[ 3 ] )
{
	h      = h - std::floor( h );
	float c = v * s, x = c * ( 1.0f - std::fabs( std::fmod( h * 6.0f, 2.0f ) - 1.0f ) ), m = v - c;
	float r = 0, g = 0, b = 0;
	int i = (int)( h * 6.0f ) % 6;
	if( i == 0 ) { r = c; g = x; } else if( i == 1 ) { r = x; g = c; } else if( i == 2 ) { g = c; b = x; }
	else if( i == 3 ) { g = x; b = c; } else if( i == 4 ) { r = x; b = c; } else { r = c; b = x; }
	out[ 0 ] = r + m; out[ 1 ] = g + m; out[ 2 ] = b + m;
}

Cutup::Cutup() : startTime( std::chrono::steady_clock::now() )
{
	SetMinInputs( 1 );
	SetMaxInputs( 1 );

	auto option = [ & ]( FFUInt32 id, const char* name, int n, const char* const* names, int def ) {
		SetOptionParamInfo( id, name, n, (float)def );
		for( int i = 0; i < n; ++i ) SetParamElementInfo( id, i, names[ i ], (float)i );
		values[ id ] = (float)def;
	};
	auto slider = [ & ]( FFUInt32 id, const char* name, float def ) {
		SetParamInfo( id, name, FF_TYPE_STANDARD, def );
		values[ id ] = def;
	};
	auto toggle = [ & ]( FFUInt32 id, const char* name, bool def ) {
		SetParamInfo( id, name, FF_TYPE_BOOLEAN, def );
		values[ id ] = def ? 1.0f : 0.0f;
	};

	SetParamInfof( PT_NEXT, "Next", FF_TYPE_EVENT );
	toggle( PT_AUTO, "Auto on Beat", true );
	option( PT_DIVISION, "Cut Every", 7, kDivisionNames, 2 );
	option( PT_SCENE_ORDER, "Scene Order", 3, kOrderNames, 2 );
	option( PT_OUTPUT, "Output", 3, kOutputNames, 0 );
	option( PT_BACKGROUND, "Background", 5, kBgNames, 0 );
	SetParamInfo( PT_HUE, "Color", FF_TYPE_HUE, hue );
	SetParamInfo( PT_SAT, "Color_Sat", FF_TYPE_SATURATION, sat );
	SetParamInfo( PT_BRI, "Color_Bri", FF_TYPE_BRIGHTNESS, bri );
	option( PT_TEXT_ORDER, "Text Order", 4, kTextOrderNames, 0 );
	option( PT_REVEAL, "Letter Reveal", 5, kRevealNames, 0 );
	const char* fonts[ kFontCount ];
	for( int i = 0; i < kFontCount; ++i ) fonts[ i ] = kFontNames[ i ];
	option( PT_FONT, "Font", kFontCount, fonts, 0 );
	slider( PT_TEXT_SIZE, "Text Size", settings.textSize );
	option( PT_TEXT_POS, "Text Position", 3, kPosNames, 1 );
	slider( PT_TINT, "Text Tint", settings.tint );
	toggle( PT_UPPER, "Uppercase", true );
	option( PT_OVERLAY, "Caption", 2, kOverlayNames, 0 );
	slider( PT_FLASH, "Flash Chance", settings.flash );
	slider( PT_SPEED, "Anim Speed", settings.speed );
	slider( PT_SHADOW, "Card Shadow", settings.shadow );

	for( int i = 0; i < kSceneCount; ++i )
	{
		toggle( PT_SCENE0 + i, kSceneNames[ i ], true );
		SetParamGroup( PT_SCENE0 + i, "Scenes" );
	}
	for( int i = 0; i < kTextCount; ++i )
	{
		std::string name = "Text " + std::to_string( i + 1 );
		settings.texts[ i ] = kDefaultTexts[ i ];
		SetParamInfo( PT_TEXT0 + i, name.c_str(), FF_TYPE_TEXT, kDefaultTexts[ i ] );
		SetParamGroup( PT_TEXT0 + i, "Texts" );
	}
	for( FFUInt32 id : { PT_TEXT_ORDER, PT_REVEAL, PT_FONT, PT_TEXT_SIZE, PT_TEXT_POS, PT_TINT, PT_UPPER, PT_OVERLAY } )
		SetParamGroup( id, "Text Style" );
	FFGLLog::LogToHost( "Cutup created" );
}

Cutup::~Cutup()
{
}

FFResult Cutup::InitGL( const FFGLViewportStruct* vp )
{
	if( !shader.Compile( cutup::vertexShader, cutup::fragmentShader ) || !quad.Initialise() )
	{
		FFGLLog::LogToHost( "Cutup: failed to compile shader" );
		DeInitGL();
		return FF_FAIL;
	}
	glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
	glGenTextures( 1, &atlasTex );
	{
		Scoped2DTextureBinding b( atlasTex );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
		glTexImage2D( GL_TEXTURE_2D, 0, GL_R8, kAtlasW, kAtlasH, 0, GL_RED, GL_UNSIGNED_BYTE, kAtlas );
	}
	glGenTextures( 1, &primTex );
	{
		Scoped2DTextureBinding b( primTex );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA32F, 4, kMaxPrims, 0, GL_RGBA, GL_FLOAT, nullptr );
	}
	glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );
	return CFFGLPlugin::InitGL( vp );
}

FFResult Cutup::DeInitGL()
{
	shader.FreeGLResources();
	quad.Release();
	if( atlasTex ) glDeleteTextures( 1, &atlasTex );
	if( primTex ) glDeleteTextures( 1, &primTex );
	atlasTex = primTex = 0;
	return FF_SUCCESS;
}

FFResult Cutup::ProcessOpenGL( ProcessOpenGLStruct* pGL )
{
	if( pGL->numInputTextures < 1 || pGL->inputTextures[ 0 ] == nullptr )
		return FF_FAIL;
	const FFGLTextureStruct& input = *pGL->inputTextures[ 0 ];
	FFGLTexCoords maxCoords        = GetMaxGLTexCoords( input );

	// gather settings
	settings.autoBeat      = values[ PT_AUTO ] > 0.5f;
	settings.divisionBeats = kDivisionBeats[ division ];
	settings.sceneOrder    = (int)values[ PT_SCENE_ORDER ];
	settings.textOrder     = (int)values[ PT_TEXT_ORDER ];
	settings.reveal        = (int)values[ PT_REVEAL ];
	settings.font          = (int)values[ PT_FONT ];
	settings.textSize      = values[ PT_TEXT_SIZE ];
	settings.textPos       = (int)values[ PT_TEXT_POS ];
	settings.tint          = values[ PT_TINT ];
	settings.uppercase     = values[ PT_UPPER ] > 0.5f;
	settings.overlay       = (int)values[ PT_OVERLAY ];
	settings.flash         = values[ PT_FLASH ];
	settings.speed         = values[ PT_SPEED ];
	settings.shadow        = values[ PT_SHADOW ];
	for( int i = 0; i < kSceneCount; ++i ) settings.sceneOn[ i ] = values[ PT_SCENE0 + i ] > 0.5f;

	if( barPhase != lastBarPhase && lastBarPhase >= 0.0f ) hostBeat = true;
	lastBarPhase = barPhase;
	double now   = std::chrono::duration< double >( std::chrono::steady_clock::now() - startTime ).count();
	float aspect = (float)currentViewport.width / (float)std::max( 1u, currentViewport.height );
	engine.Update( now, bpm, hostBeat ? barPhase : -1.0f, aspect, settings );

	// upload primitives (4 texels per primitive)
	const auto& prims = engine.Prims();
	int count         = (int)prims.size();
	if( count > 0 )
	{
		Scoped2DTextureBinding b( primTex );
		glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, 4, count, GL_RGBA, GL_FLOAT, prims.data() );
	}

	float col[ 3 ];
	HsbToRgb( hue, sat, bri, col );

	ScopedShaderBinding shaderBinding( shader.GetGLID() );
	ScopedSamplerActivation s0( 0 );
	Scoped2DTextureBinding t0( input.Handle );
	ScopedSamplerActivation s1( 1 );
	Scoped2DTextureBinding t1( atlasTex );
	ScopedSamplerActivation s2( 2 );
	Scoped2DTextureBinding t2( primTex );
	shader.Set( "InputTexture", 0 );
	shader.Set( "Atlas", 1 );
	shader.Set( "Prims", 2 );
	shader.Set( "MaxUV", maxCoords.s, maxCoords.t );
	shader.Set( "Resolution", (float)currentViewport.width, (float)currentViewport.height );
	shader.Set( "AtlasSize", (float)kAtlasW, (float)kAtlasH );
	shader.Set( "Count", count );
	shader.Set( "TextColor", col[ 0 ], col[ 1 ], col[ 2 ] );
	shader.Set( "Output", output );
	shader.Set( "Background", background );
	quad.Draw();
	return FF_SUCCESS;
}

FFResult Cutup::SetFloatParameter( unsigned int index, float value )
{
	if( index >= PT_TEXT0 ) return FF_FAIL;
	switch( index )
	{
	case PT_NEXT:
		if( value > 0.5f ) engine.Trigger();
		return FF_SUCCESS;
	case PT_HUE: hue = value; return FF_SUCCESS;
	case PT_SAT: sat = value; return FF_SUCCESS;
	case PT_BRI: bri = value; return FF_SUCCESS;
	case PT_DIVISION: division = std::min( 6, std::max( 0, (int)std::lround( value ) ) ); break;
	case PT_OUTPUT: output = std::min( 2, std::max( 0, (int)std::lround( value ) ) ); break;
	case PT_BACKGROUND: background = std::min( 4, std::max( 0, (int)std::lround( value ) ) ); break;
	default: break;
	}
	values[ index ] = value;
	// option params arrive as element values; keep them integral
	if( index == PT_SCENE_ORDER || index == PT_TEXT_ORDER || index == PT_REVEAL || index == PT_FONT ||
	    index == PT_TEXT_POS || index == PT_OVERLAY )
		values[ index ] = (float)std::lround( value );
	if( index == PT_FONT ) values[ index ] = std::min( (float)( kFontCount - 1 ), std::max( 0.0f, values[ index ] ) );
	return FF_SUCCESS;
}

float Cutup::GetFloatParameter( unsigned int index )
{
	switch( index )
	{
	case PT_NEXT: return 0.0f;
	case PT_HUE: return hue;
	case PT_SAT: return sat;
	case PT_BRI: return bri;
	case PT_DIVISION: return (float)division;
	case PT_OUTPUT: return (float)output;
	case PT_BACKGROUND: return (float)background;
	}
	return index < PT_TEXT0 ? values[ index ] : 0.0f;
}

FFResult Cutup::SetTextParameter( unsigned int index, const char* value )
{
	if( index < PT_TEXT0 || index >= PT_COUNT ) return FF_FAIL;
	settings.texts[ index - PT_TEXT0 ] = value ? value : "";
	return FF_SUCCESS;
}

char* Cutup::GetTextParameter( unsigned int index )
{
	if( index < PT_TEXT0 || index >= PT_COUNT ) return nullptr;
	return const_cast< char* >( settings.texts[ index - PT_TEXT0 ].c_str() );
}
