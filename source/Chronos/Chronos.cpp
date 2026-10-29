#include "Chronos.h"
#include "Shaders.h"
#include <ffglex/FFGLScopedFBOBinding.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace ffglex;

enum ParamID : FFUInt32
{
	PT_MODE,
	PT_TIMEMAP,
	PT_DEPTH,
	PT_A,
	PT_B,
	PT_REVERSE,
	PT_DIVISION,
	PT_FREEZE,
	PT_MIX,
	PT_FRAMES,
	PT_RES,
	PT_COUNT
};

static CFFGLPluginInfo PluginInfo(
	PluginFactory< Chronos >,
	"CHR8",
	"Chronos",
	2, 1,// FFGL API version
	2, 0,// plugin version
	FF_EFFECT,
	"20 time machine effects: Slit-Scan, Rutt-Etra, Datamosh, Reaction-Diffusion, Beat Stutter, VHS Rewind, Video Scratch and more",
	"CHRONOS FFGL - native frame history effects" );

static const char* kModeNames[ kModeCount ] = {
	"Slit-Scan", "Chroma Time Split", "Beat Stutter", "Chrono Trails",
	"Rutt-Etra", "Time Mosaic", "Long Exposure", "Time Tunnel",
	"Datamosh", "Pixel Melt", "Reaction-Diffusion", "Infinite Feedback",
	"VHS Rewind", "Video Scratch", "Beat Ripple", "Slice Shift",
	"Strobe Echo", "Neon Echo", "Time Kaleido", "Motion Key / Cloak"
};
// Display names of the two shared knobs, per mode.
static const char* kParamNames[ kModeCount ][ 2 ] = {
	{ "Map Repeat", "Map Speed" },
	{ "Spread", "Angle" },
	{ "Loop Length", "Chance" },
	{ "Sensitivity", "Video Level" },
	{ "Height", "Lines" },
	{ "Block Size", "Chance" },
	{ "Light Paint", "Decay" },
	{ "Zoom", "Key Level" },
	{ "Mosh Strength", "I-Frame Chance" },
	{ "Threshold", "Length" },
	{ "Pattern", "Video Influence" },
	{ "Zoom / Spin", "Persistence" },
	{ "Rewind Speed", "Tape Damage" },
	{ "Scratch Shape", "Double Time" },
	{ "Wave Strength", "Random Center" },
	{ "Bands", "Chance" },
	{ "Copies", "Falloff" },
	{ "Sensitivity", "Video Level" },
	{ "Segments", "Spin" },
	{ "Sensitivity", "Key / Cloak" },
};
static const float kParamDefaults[ kModeCount ][ 2 ] = {
	{ 0.0f, 0.0f },
	{ 0.3f, 0.0f },
	{ 0.3f, 0.5f },
	{ 0.6f, 0.6f },
	{ 0.4f, 0.4f },
	{ 0.4f, 0.35f },
	{ 0.0f, 0.3f },
	{ 0.35f, 0.25f },
	{ 0.4f, 0.15f },
	{ 0.55f, 0.35f },
	{ 0.3f, 0.6f },
	{ 0.65f, 0.6f },
	{ 0.35f, 0.5f },
	{ 0.4f, 0.0f },
	{ 0.6f, 0.3f },
	{ 0.4f, 0.35f },
	{ 0.4f, 0.4f },
	{ 0.6f, 0.35f },
	{ 0.25f, 0.6f },
	{ 0.5f, 0.0f },
};
static const char* kTimeMapNames[ 6 ] = { "Left > Right", "Top > Bottom", "Radial", "Spiral", "Noise", "Luma" };
static const char* kDivisionNames[ 5 ] = { "1/16", "1/8", "1/4", "1/2", "1 Bar" };
static const float kDivisionBeats[ 5 ] = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };
static const char* kFramesNames[ 4 ]   = { "16", "32", "64", "128" };
static const int kFramesValues[ 4 ]    = { 16, 32, 64, 128 };
static const char* kResNames[ 3 ]      = { "Full", "Half", "Quarter" };

Chronos::Chronos() :
	startTime( std::chrono::steady_clock::now() ),
	rng( 1337u )
{
	SetMinInputs( 1 );
	SetMaxInputs( 1 );

	for( int m = 0; m < kModeCount; ++m )
	{
		paramStore[ m ][ 0 ] = kParamDefaults[ m ][ 0 ];
		paramStore[ m ][ 1 ] = kParamDefaults[ m ][ 1 ];
	}
	paramA = paramStore[ 0 ][ 0 ];
	paramB = paramStore[ 0 ][ 1 ];

	SetOptionParamInfo( PT_MODE, "Mode", kModeCount, 0.0f );
	for( int i = 0; i < kModeCount; ++i )
		SetParamElementInfo( PT_MODE, i, kModeNames[ i ], (float)i );
	AddElementSeparator( PT_MODE, 3 );
	AddElementSeparator( PT_MODE, 6 );
	AddElementSeparator( PT_MODE, 8 ); // feedback & glitch
	AddElementSeparator( PT_MODE, 12 );// beat & playback
	AddElementSeparator( PT_MODE, 16 );// echoes & keys

	SetOptionParamInfo( PT_TIMEMAP, "Time Map", 6, 0.0f );
	for( int i = 0; i < 6; ++i )
		SetParamElementInfo( PT_TIMEMAP, i, kTimeMapNames[ i ], (float)i );

	SetParamInfo( PT_DEPTH, "Depth", FF_TYPE_STANDARD, depth );
	SetParamInfo( PT_A, "Param A", FF_TYPE_STANDARD, paramA );
	SetParamInfo( PT_B, "Param B", FF_TYPE_STANDARD, paramB );
	SetParamInfo( PT_REVERSE, "Reverse Time", FF_TYPE_BOOLEAN, false );

	SetOptionParamInfo( PT_DIVISION, "Beat Division", 5, (float)division );
	for( int i = 0; i < 5; ++i )
		SetParamElementInfo( PT_DIVISION, i, kDivisionNames[ i ], (float)i );

	SetParamInfo( PT_FREEZE, "Freeze Buffer", FF_TYPE_BOOLEAN, false );
	SetParamInfo( PT_MIX, "Mix", FF_TYPE_STANDARD, mixAmount );

	SetOptionParamInfo( PT_FRAMES, "Buffer Frames", 4, (float)framesOpt );
	for( int i = 0; i < 4; ++i )
		SetParamElementInfo( PT_FRAMES, i, kFramesNames[ i ], (float)i );
	SetOptionParamInfo( PT_RES, "Buffer Res", 3, (float)resOpt );
	for( int i = 0; i < 3; ++i )
		SetParamElementInfo( PT_RES, i, kResNames[ i ], (float)i );
	SetParamGroup( PT_FRAMES, "Buffer" );
	SetParamGroup( PT_RES, "Buffer" );

	ApplyModeUI( false );
	FFGLLog::LogToHost( "Chronos created" );
}

Chronos::~Chronos()
{
}

FFResult Chronos::InitGL( const FFGLViewportStruct* vp )
{
	if( !mainShader.Compile( chronos::vertexShader, chronos::mainShader ) ||
	    !copyShader.Compile( chronos::vertexShader, chronos::copyShader ) ||
	    !stateShader.Compile( chronos::vertexShader, chronos::stateShader ) ||
	    !quad.Initialise() )
	{
		FFGLLog::LogToHost( "Chronos: failed to initialise GL resources" );
		DeInitGL();
		return FF_FAIL;
	}
	glGenFramebuffers( 1, &fbo );
	return CFFGLPlugin::InitGL( vp );
}

FFResult Chronos::DeInitGL()
{
	ReleaseHistory();
	if( fbo )
		glDeleteFramebuffers( 1, &fbo );
	fbo = 0;
	mainShader.FreeGLResources();
	copyShader.FreeGLResources();
	stateShader.FreeGLResources();
	quad.Release();
	return FF_SUCCESS;
}

void Chronos::ReleaseHistory()
{
	if( historyTex )
		glDeleteTextures( 1, &historyTex );
	historyTex = 0;
	if( stateTex[ 0 ] )
		glDeleteTextures( 2, stateTex );
	stateTex[ 0 ] = stateTex[ 1 ] = 0;
	histW = histH = histN = 0;
	head  = -1;
	valid = 0;
}

bool Chronos::EnsureHistory( GLsizei width, GLsizei height )
{
	int div    = 1 << resOpt;
	GLsizei w  = std::max< GLsizei >( 1, width / div );
	GLsizei h  = std::max< GLsizei >( 1, height / div );
	GLsizei n  = kFramesValues[ framesOpt ];
	if( historyTex && w == histW && h == histH && n == histN )
		return true;

	ReleaseHistory();
	glGenTextures( 1, &historyTex );
	ScopedTextureBinding bind( GL_TEXTURE_2D_ARRAY, historyTex );
	glTexParameteri( GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glTexImage3D( GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, w, h, n, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
	if( glGetError() != GL_NO_ERROR )
	{
		FFGLLog::LogToHost( "Chronos: could not allocate history buffer (out of VRAM?) - lower Buffer Frames/Res" );
		glDeleteTextures( 1, &historyTex );
		historyTex = 0;
		return false;
	}
	histW = w;
	histH = h;
	histN = n;

	// two half-float textures for the feedback modes
	glGenTextures( 2, stateTex );
	for( int i = 0; i < 2; ++i )
	{
		Scoped2DTextureBinding sb( stateTex[ i ] );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr );
	}
	stateDirty = true;
	return glGetError() == GL_NO_ERROR;
}

void Chronos::ClearState()
{
	ScopedFBOBinding fboBinding( fbo, ScopedFBOBinding::RB_REVERT );
	for( int i = 0; i < 2; ++i )
	{
		glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, stateTex[ i ], 0 );
		glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
		glClear( GL_COLOR_BUFFER_BIT );
	}
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0 );
	stateDirty     = false;
	refreshPending = true;
}

void Chronos::UpdateBeat()
{
	// Prefer the host's transport (SetBeatInfo). Fall back to an internal clock at the host bpm.
	if( barPhase != lastBarPhase )
		hostBeat = true;
	lastBarPhase = barPhase;

	double beatPos;
	if( hostBeat )
		beatPos = (double)barPhase * 4.0;
	else
	{
		double secs = std::chrono::duration< double >( std::chrono::steady_clock::now() - startTime ).count();
		beatPos     = std::fmod( secs * ( bpm > 1.0f ? bpm : 120.0f ) / 60.0, 4.0 );
	}

	float divBeats = kDivisionBeats[ division ];
	long long idx  = (long long)std::floor( beatPos / divBeats );
	beatPhase      = (float)( beatPos / divBeats - std::floor( beatPos / divBeats ) );
	bool wrapped   = lastBeatPos >= 0.0 && beatPos + 1e-6 < lastBeatPos;
	lastBeatPos    = beatPos;

	if( idx != sliceIndex || wrapped )
	{
		sliceIndex = idx;
		sliceCounter += 1.0f;
		std::uniform_real_distribution< float > dist( 0.0f, 1.0f );
		float chance = mode == 2 ? paramB : 0.0f;
		stuttering   = dist( rng ) < chance;
		if( mode == 8 && dist( rng ) < paramB )
			refreshPending = true;// Datamosh I-frame on the beat
		sliceStart   = frameCounter;
		int maxLoop  = std::max( 2, valid / 2 );
		loopLen      = std::max( 2, (int)std::round( 2.0f + paramA * paramA * ( maxLoop - 2 ) ) );
	}

	stutterAge = 0.0f;
	if( stuttering && mode == 2 )
	{
		unsigned long long k = ( frameCounter - sliceStart ) % (unsigned long long)loopLen;
		long long src        = reverse ? (long long)sliceStart - 1 - (long long)k
		                                 : (long long)sliceStart - loopLen + (long long)k;
		stutterAge           = (float)std::max< long long >( 0, (long long)frameCounter - src );
	}
}

FFResult Chronos::ProcessOpenGL( ProcessOpenGLStruct* pGL )
{
	if( pGL->numInputTextures < 1 || pGL->inputTextures[ 0 ] == nullptr )
		return FF_FAIL;
	const FFGLTextureStruct& input = *pGL->inputTextures[ 0 ];
	FFGLTexCoords maxCoords        = GetMaxGLTexCoords( input );

	if( !EnsureHistory( (GLsizei)input.Width, (GLsizei)input.Height ) )
		return FF_FAIL;

	++frameCounter;

	// 1) record the incoming frame into the ring buffer
	if( !freeze || valid == 0 )
	{
		head  = ( head + 1 ) % histN;
		valid = std::min( valid + 1, (int)histN );

		ScopedFBOBinding fboBinding( fbo, ScopedFBOBinding::RB_REVERT );
		glFramebufferTextureLayer( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, historyTex, 0, head );
		glViewport( 0, 0, histW, histH );

		ScopedShaderBinding shaderBinding( copyShader.GetGLID() );
		ScopedSamplerActivation sampler( 0 );
		Scoped2DTextureBinding texBinding( input.Handle );
		copyShader.Set( "InputTexture", 0 );
		copyShader.Set( "MaxUV", maxCoords.s, maxCoords.t );
		quad.Draw();

		glFramebufferTextureLayer( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 0, 0, 0 );
	}
	glViewport( currentViewport.x, currentViewport.y, currentViewport.width, currentViewport.height );

	UpdateBeat();

	// 2) feedback modes: advance the state simulation (ping-pong)
	if( IsStateful( mode ) )
	{
		if( stateDirty )
			ClearState();
		int iterations = mode == 10 ? 8 : 1;
		ScopedFBOBinding fboBinding( fbo, ScopedFBOBinding::RB_REVERT );
		glViewport( 0, 0, histW, histH );
		ScopedShaderBinding sb( stateShader.GetGLID() );
		ScopedSamplerActivation s0( 0 );
		Scoped2DTextureBinding t0( input.Handle );
		ScopedSamplerActivation s1( 1 );
		ScopedTextureBinding t1( GL_TEXTURE_2D_ARRAY, historyTex );
		stateShader.Set( "InputTexture", 0 );
		stateShader.Set( "History", 1 );
		stateShader.Set( "State", 2 );
		stateShader.Set( "MaxUV", maxCoords.s, maxCoords.t );
		stateShader.Set( "Resolution", (float)currentViewport.width, (float)currentViewport.height );
		stateShader.Set( "StateRes", (float)histW, (float)histH );
		stateShader.Set( "Mode", mode );
		stateShader.Set( "Head", head );
		stateShader.Set( "Frames", (int)histN );
		stateShader.Set( "Valid", valid );
		stateShader.Set( "ParamA", paramA );
		stateShader.Set( "ParamB", paramB );
		stateShader.Set( "Reverse", reverse ? 1.0f : 0.0f );
		stateShader.Set( "Time", std::fmod( std::chrono::duration< float >( std::chrono::steady_clock::now() - startTime ).count(), 3600.0f ) );
		stateShader.Set( "Refresh", refreshPending ? 1.0f : 0.0f );
		refreshPending = false;
		for( int i = 0; i < iterations; ++i )
		{
			int dst = 1 - stateCur;
			glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, stateTex[ dst ], 0 );
			ScopedSamplerActivation s2( 2 );
			Scoped2DTextureBinding t2( stateTex[ stateCur ] );
			quad.Draw();
			stateCur = dst;
		}
		glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0 );
		glViewport( currentViewport.x, currentViewport.y, currentViewport.width, currentViewport.height );
	}

	// 3) render the effect into the host's framebuffer
	ScopedShaderBinding shaderBinding( mainShader.GetGLID() );
	ScopedSamplerActivation sampler0( 0 );
	Scoped2DTextureBinding inBinding( input.Handle );
	ScopedSamplerActivation sampler1( 1 );
	ScopedTextureBinding histBinding( GL_TEXTURE_2D_ARRAY, historyTex );
	ScopedSamplerActivation sampler2( 2 );
	Scoped2DTextureBinding stateBinding( stateTex[ stateCur ] );

	float t = std::chrono::duration< float >( std::chrono::steady_clock::now() - startTime ).count();

	mainShader.Set( "InputTexture", 0 );
	mainShader.Set( "History", 1 );
	mainShader.Set( "State", 2 );
	mainShader.Set( "StateRes", (float)histW, (float)histH );
	mainShader.Set( "FrameCount", (float)( frameCounter % 100000ULL ) );
	mainShader.Set( "BeatPhase", beatPhase );
	mainShader.Set( "SliceAge", (float)std::min< unsigned long long >( frameCounter - sliceStart, (unsigned long long)std::max( valid - 1, 0 ) ) );
	mainShader.Set( "MaxUV", maxCoords.s, maxCoords.t );
	mainShader.Set( "Resolution", (float)currentViewport.width, (float)currentViewport.height );
	mainShader.Set( "Mode", mode );
	mainShader.Set( "TimeMap", timeMap );
	mainShader.Set( "Head", head );
	mainShader.Set( "Frames", (int)histN );
	mainShader.Set( "Valid", valid );
	mainShader.Set( "Depth", depth );
	mainShader.Set( "ParamA", paramA );
	mainShader.Set( "ParamB", paramB );
	mainShader.Set( "Reverse", reverse ? 1.0f : 0.0f );
	mainShader.Set( "Mix", mixAmount );
	mainShader.Set( "Time", std::fmod( t, 3600.0f ) );
	mainShader.Set( "StutterAge", stutterAge );
	mainShader.Set( "Slice", std::fmod( sliceCounter, 997.0f ) );
	quad.Draw();

	return FF_SUCCESS;
}

void Chronos::ApplyModeUI( bool raiseEvents )
{
	SetParamDisplayName( PT_A, kParamNames[ mode ][ 0 ], raiseEvents );
	SetParamDisplayName( PT_B, kParamNames[ mode ][ 1 ], raiseEvents );
	SetParamVisibility( PT_TIMEMAP, mode == 0, raiseEvents );
	bool beatMode = mode == 2 || mode == 5 || mode == 8 || mode == 13 || mode == 14 || mode == 15;
	SetParamVisibility( PT_DIVISION, beatMode, raiseEvents );
	SetParamVisibility( PT_DEPTH, mode != 2 && !IsStateful( mode ), raiseEvents );
}

FFResult Chronos::SetFloatParameter( unsigned int index, float value )
{
	switch( index )
	{
	case PT_MODE:
	{
		int m = std::min( kModeCount - 1, std::max( 0, (int)std::lround( value ) ) );
		if( m != mode )
		{
			paramStore[ mode ][ 0 ] = paramA;
			paramStore[ mode ][ 1 ] = paramB;
			mode                    = m;
			paramA                  = paramStore[ mode ][ 0 ];
			paramB                  = paramStore[ mode ][ 1 ];
			if( IsStateful( mode ) )
				stateDirty = true;// start the simulation fresh
			ApplyModeUI( true );
			RaiseParamEvent( PT_A, FF_EVENT_FLAG_VALUE );
			RaiseParamEvent( PT_B, FF_EVENT_FLAG_VALUE );
		}
		break;
	}
	case PT_TIMEMAP:  timeMap   = std::min( 5, std::max( 0, (int)std::lround( value ) ) ); break;
	case PT_DEPTH:    depth     = value; break;
	case PT_A:        paramA    = value; break;
	case PT_B:        paramB    = value; break;
	case PT_REVERSE:  reverse   = value > 0.5f; break;
	case PT_DIVISION: division  = std::min( 4, std::max( 0, (int)std::lround( value ) ) ); break;
	case PT_FREEZE:   freeze    = value > 0.5f; break;
	case PT_MIX:      mixAmount = value; break;
	case PT_FRAMES:   framesOpt = std::min( 3, std::max( 0, (int)std::lround( value ) ) ); break;
	case PT_RES:      resOpt    = std::min( 2, std::max( 0, (int)std::lround( value ) ) ); break;
	default: return FF_FAIL;
	}
	return FF_SUCCESS;
}

float Chronos::GetFloatParameter( unsigned int index )
{
	switch( index )
	{
	case PT_MODE:     return (float)mode;
	case PT_TIMEMAP:  return (float)timeMap;
	case PT_DEPTH:    return depth;
	case PT_A:        return paramA;
	case PT_B:        return paramB;
	case PT_REVERSE:  return reverse ? 1.0f : 0.0f;
	case PT_DIVISION: return (float)division;
	case PT_FREEZE:   return freeze ? 1.0f : 0.0f;
	case PT_MIX:      return mixAmount;
	case PT_FRAMES:   return (float)framesOpt;
	case PT_RES:      return (float)resOpt;
	}
	return 0.0f;
}

char* Chronos::GetParameterDisplay( unsigned int index )
{
	if( index == PT_DEPTH )
	{
		int frames = (int)std::lround( depth * ( kFramesValues[ framesOpt ] - 1 ) );
		std::snprintf( displayBuf, sizeof( displayBuf ), "%d frames", frames );
		return displayBuf;
	}
	return CFFGLPlugin::GetParameterDisplay( index );
}
