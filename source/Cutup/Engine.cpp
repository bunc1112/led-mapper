#include "Engine.h"
#include "FontAtlas.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace cutup
{
const char* kSceneNames[ kSceneCount ] = {
	"Word Reveal", "Word Knockout", "Full Frame", "Card Cascade", "Card Zoom",
	"Card Scatter", "Iris", "Blinds", "Grid"
};

// ------------------------------------------------------------------------------------------ helpers
static float clamp01( float x ) { return x < 0.0f ? 0.0f : ( x > 1.0f ? 1.0f : x ); }
static float easeOutCubic( float x ) { x = clamp01( x ); return 1.0f - std::pow( 1.0f - x, 3.0f ); }
static float easeInOutCubic( float x ) { x = clamp01( x ); return x < 0.5f ? 4.0f * x * x * x : 1.0f - std::pow( -2.0f * x + 2.0f, 3.0f ) / 2.0f; }
static float easeOutExpo( float x ) { x = clamp01( x ); return x >= 1.0f ? 1.0f : 1.0f - std::pow( 2.0f, -10.0f * x ); }
static float easeOutBack( float x )
{
	x = clamp01( x );
	const float c1 = 1.70158f, c3 = c1 + 1.0f;
	return 1.0f + c3 * std::pow( x - 1.0f, 3.0f ) + c1 * std::pow( x - 1.0f, 2.0f );
}
static float hashf( float a, float b )
{
	float h = std::sin( a * 127.1f + b * 311.7f ) * 43758.5453f;
	return h - std::floor( h );
}

static const Glyph* FindGlyph( int font, char32_t cp )
{
	static std::unordered_map< unsigned long long, int > map;
	if( map.empty() )
		for( int i = 0; i < kGlyphCount; ++i )
			map[ ( (unsigned long long)kGlyphs[ i ].font << 32 ) | kGlyphs[ i ].cp ] = i;
	auto it = map.find( ( (unsigned long long)font << 32 ) | (unsigned long long)cp );
	return it == map.end() ? nullptr : &kGlyphs[ it->second ];
}

static char32_t ToUpper( char32_t c )
{
	if( c >= 'a' && c <= 'z' ) return c - 32;
	if( c >= 0xE0 && c <= 0xFE && c != 0xF7 ) return c - 32;
	if( c >= 0x100 && c <= 0x17F )
	{
		bool lowerIsEven = ( c >= 0x139 && c <= 0x148 ) || ( c >= 0x179 && c <= 0x17E );
		if( lowerIsEven ) return ( c % 2 == 0 ) ? c - 1 : c;
		return ( c % 2 == 1 ) ? c - 1 : c;
	}
	if( c == 0x153 ) return 0x152;
	return c;
}

std::u32string Utf8ToU32( const std::string& s )
{
	std::u32string out;
	for( size_t i = 0; i < s.size(); )
	{
		unsigned char c = (unsigned char)s[ i ];
		char32_t cp; int n;
		if( c < 0x80 ) { cp = c; n = 1; }
		else if( ( c >> 5 ) == 0x6 ) { cp = c & 0x1F; n = 2; }
		else if( ( c >> 4 ) == 0xE ) { cp = c & 0x0F; n = 3; }
		else if( ( c >> 3 ) == 0x1E ) { cp = c & 0x07; n = 4; }
		else { ++i; continue; }
		if( i + n > s.size() ) break;
		for( int k = 1; k < n; ++k ) cp = ( cp << 6 ) | ( (unsigned char)s[ i + k ] & 0x3F );
		out.push_back( cp );
		i += n;
	}
	return out;
}

// ------------------------------------------------------------------------------------------ engine
Engine::Engine() : rng( 20261030u ), dist( 0.0f, 1.0f )
{
	prims.reserve( kMaxPrims );
	for( float& r : rnd ) r = 0.5f;
}

void Engine::Trigger()
{
	pendingTrigger = true;
}

int Engine::PickScene( const Settings& s )
{
	std::vector< int > on;
	for( int i = 0; i < kSceneCount; ++i )
		if( s.sceneOn[ i ] ) on.push_back( i );
	if( on.empty() ) on.push_back( SC_WORD_REVEAL );

	if( s.sceneOrder == 0 )
	{
		for( int k = 1; k <= kSceneCount; ++k )
		{
			int c = ( seqScene + k ) % kSceneCount;
			if( std::find( on.begin(), on.end(), c ) != on.end() ) { seqScene = c; return c; }
		}
		return on[ 0 ];
	}
	if( s.sceneOrder == 1 )
	{
		int c = on[ std::min( (int)on.size() - 1, (int)( Rand() * on.size() ) ) ];
		if( on.size() > 1 && c == scene ) c = on[ ( std::find( on.begin(), on.end(), c ) - on.begin() + 1 ) % on.size() ];
		return c;
	}
	// shuffle bag: every enabled scene once, in random order, no immediate repeats
	sceneBag.erase( std::remove_if( sceneBag.begin(), sceneBag.end(), [ & ]( int c ) { return std::find( on.begin(), on.end(), c ) == on.end(); } ), sceneBag.end() );
	if( sceneBag.empty() )
	{
		sceneBag = on;
		std::shuffle( sceneBag.begin(), sceneBag.end(), rng );
		if( sceneBag.size() > 1 && sceneBag.back() == scene ) std::swap( sceneBag.front(), sceneBag.back() );
	}
	int c = sceneBag.back();
	sceneBag.pop_back();
	return c;
}

void Engine::PickWord( const Settings& s )
{
	std::vector< int > used;
	for( int i = 0; i < kTextCount; ++i )
		if( !s.texts[ i ].empty() ) used.push_back( i );
	if( used.empty() ) { word = U"CUTUP"; return; }

	int idx;
	auto pos = std::find( used.begin(), used.end(), wordIndex );
	int cur  = pos == used.end() ? -1 : (int)( pos - used.begin() );
	switch( s.textOrder )
	{
	case 1: // random, no immediate repeat
		idx = (int)( Rand() * used.size() ) % (int)used.size();
		if( used.size() > 1 && idx == cur ) idx = ( idx + 1 ) % used.size();
		break;
	case 2: // shuffle bag
		textBag.erase( std::remove_if( textBag.begin(), textBag.end(), [ & ]( int c ) { return std::find( used.begin(), used.end(), c ) == used.end(); } ), textBag.end() );
		if( textBag.empty() )
		{
			textBag = used;
			std::shuffle( textBag.begin(), textBag.end(), rng );
			if( textBag.size() > 1 && textBag.back() == wordIndex ) std::swap( textBag.front(), textBag.back() );
		}
		wordIndex = textBag.back();
		textBag.pop_back();
		idx = -1;
		break;
	case 3: // ping-pong
		if( used.size() == 1 ) idx = 0;
		else
		{
			idx = cur + pingDir;
			if( idx >= (int)used.size() ) { pingDir = -1; idx = (int)used.size() - 2; }
			if( idx < 0 ) { pingDir = 1; idx = cur < 0 ? 0 : 1; }
		}
		break;
	default: // sequential
		idx = ( cur + 1 ) % (int)used.size();
	}
	if( idx >= 0 ) wordIndex = used[ idx ];
	word = Utf8ToU32( s.texts[ wordIndex ] );
	if( s.uppercase )
		for( auto& c : word ) c = ToUpper( c );
}

void Engine::StartScene( const Settings& s, double now, float aspect )
{
	(void)aspect;
	scene      = PickScene( s );
	sceneStart = now;
	for( float& r : rnd ) r = Rand();

	bool usesText = scene == SC_WORD_REVEAL || scene == SC_WORD_KNOCKOUT || s.overlay == 1;
	if( usesText )
	{
		PickWord( s );
		if( s.textPos == 0 ) wordY = 0.5f;
		else if( s.textPos == 1 ) { const float ys[ 3 ] = { 0.24f, 0.5f, 0.76f }; wordY = ys[ (int)( Rand() * 3 ) % 3 ]; }
		else { const float ys[ 3 ] = { 0.76f, 0.5f, 0.24f }; wordY = ys[ posCycle++ % 3 ]; }
		if( scene == SC_WORD_KNOCKOUT ) wordY = 0.5f;

		int n = 0;
		for( char32_t c : word ) if( c != U' ' && c != U'|' ) ++n;
		letterDelay.assign( n, 0.0f );
		float W = 0.42f * (float)interval;
		std::vector< int > order( n );
		for( int i = 0; i < n; ++i ) order[ i ] = i;
		if( s.reveal == 0 ) std::shuffle( order.begin(), order.end(), rng );
		for( int i = 0; i < n; ++i )
			letterDelay[ i ] = s.reveal == 4 ? 0.0f : W * (float)order[ i ] / (float)std::max( n, 1 );
	}
	flashType = Rand() < s.flash ? (int)( Rand() * 3.0f ) % 3 : -1;
}

void Engine::BuildWord( const Settings& s, float t, float D, float aspect, int fill, float alphaMul, float y,
                        float sizeMul, int colorIdx, float tint, bool animate )
{
	int font          = std::min( std::max( s.font, 0 ), kFontCount - 1 );
	const Glyph* capG = FindGlyph( font, U'H' );
	float capH        = capG ? capG->pt : 0.7f;
	float em          = ( 0.08f + s.textSize * 0.42f ) * sizeMul / capH;
	float tracking    = 0.02f;

	// split into lines on '|'
	std::vector< std::u32string > lines( 1 );
	for( char32_t c : word )
	{
		if( c == U'|' ) lines.emplace_back();
		else lines.back().push_back( c );
	}
	float widest = 0.0f;
	for( auto& l : lines )
	{
		float w = 0.0f;
		for( char32_t c : l ) if( const Glyph* g = FindGlyph( font, c ) ) w += g->advance + tracking;
		widest = std::max( widest, w );
	}
	float maxW = 0.92f * aspect;
	if( widest * em > maxW ) em *= maxW / ( widest * em );
	float lineH  = capH * em * 1.22f;
	float blockH = lineH * ( lines.size() - 1 ) + capH * em;
	float top    = y + blockH * 0.5f;

	int k = 0;
	for( size_t li = 0; li < lines.size(); ++li )
	{
		float w = 0.0f;
		for( char32_t c : lines[ li ] ) if( const Glyph* g = FindGlyph( font, c ) ) w += ( g->advance + tracking ) * em;
		float penX = aspect * 0.5f - w * 0.5f;
		float base = top - capH * em - lineH * li;
		for( char32_t c : lines[ li ] )
		{
			const Glyph* g = FindGlyph( font, c );
			if( !g ) continue;
			if( c == U' ' ) { penX += ( g->advance + tracking ) * em; continue; }

			float a = 1.0f, scale = 1.0f, yoff = 0.0f;
			const Glyph* draw = g;
			if( animate )
			{
				float tl = t - ( k < (int)letterDelay.size() ? letterDelay[ k ] : 0.0f );
				switch( s.reveal )
				{
				case 2: // slide up
					a    = clamp01( tl / 0.1f );
					yoff = -( 1.0f - easeOutCubic( tl / 0.25f ) ) * capH * em * 0.9f;
					break;
				case 3: // scramble
					if( tl < 0.0f && t >= 0.0f )
					{
						static const char32_t pool[] = U"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789#%&";
						int r = (int)( hashf( (float)k, std::floor( t / 0.05f ) ) * 39.0f ) % 39;
						if( const Glyph* rg = FindGlyph( font, pool[ r ] ) ) draw = rg;
						a = 0.75f;
					}
					break;
				case 4: // all at once, punch
					a     = clamp01( tl / 0.05f );
					scale = 1.0f + 0.18f * ( 1.0f - easeOutCubic( tl / 0.25f ) );
					break;
				default: // random pop / left to right
					a     = clamp01( tl / 0.05f );
					scale = 1.0f + 0.4f * ( 1.0f - easeOutCubic( tl / 0.14f ) );
				}
			}
			if( a > 0.0f )
			{
				// keep the real glyph's box for layout, draw the (maybe scrambled) glyph centred in it
				float cx = penX + ( g->pl + g->pr ) * 0.5f * em;
				float cy = base + ( draw->pb + draw->pt ) * 0.5f * em + yoff;
				Prim p{};
				p.cx = cx; p.cy = cy;
				p.hw = ( draw->pr - draw->pl ) * 0.5f * em * scale;
				p.hh = ( draw->pt - draw->pb ) * 0.5f * em * scale;
				p.alpha = a * alphaMul;
				p.type  = PRIM_GLYPH;
				p.fill  = (float)fill;
				p.c0 = draw->u0; p.c1 = draw->v0; p.c2 = draw->u1; p.c3 = draw->v1;
				p.tint  = tint;
				p.color = (float)colorIdx;
				Add( p );
			}
			penX += ( g->advance + tracking ) * em;
			++k;
		}
	}
	(void)D;
}

void Engine::Update( double timeSec, float bpm, float barPhase, float aspect, const Settings& s )
{
	if( bpm < 20.0f ) bpm = 120.0f;
	if( lastTime < 0.0 ) lastTime = timeSec;

	// beat tracking: host bar phase if available, otherwise an internal clock at the host bpm
	if( barPhase >= 0.0f )
	{
		float pos = barPhase * 4.0f;
		if( lastBarPos >= 0.0f )
		{
			float d = pos - lastBarPos;
			if( d < -2.0f ) d += 4.0f;
			if( d > 0.0f ) beatTotal += d;
		}
		lastBarPos = pos;
	}
	else
		beatTotal += ( timeSec - lastTime ) * bpm / 60.0;
	lastTime = timeSec;

	interval        = std::max( 0.05, (double)s.divisionBeats * 60.0 / bpm );
	long long slice = (long long)std::floor( beatTotal / std::max( 0.0625f, s.divisionBeats ) );
	if( s.autoBeat && lastSlice >= 0 && slice != lastSlice ) pendingTrigger = true;
	lastSlice = slice;

	if( pendingTrigger )
	{
		StartScene( s, timeSec, aspect );
		pendingTrigger = false;
	}

	prims.clear();
	float speedMul = std::pow( 2.0f, ( s.speed - 0.5f ) * 2.0f );
	float tReal    = (float)( timeSec - sceneStart );
	float t        = tReal * speedMul;
	float D        = (float)interval;
	float u        = t / D;
	float exitFade = s.autoBeat ? 1.0f - clamp01( ( tReal / D - 0.8f ) / 0.2f ) : 1.0f;
	float a        = aspect;

	auto card = [ & ]( float cx, float cy, float hw, float hh, int i, float alpha, float rot ) {
		Prim p{};
		p.cx = cx; p.cy = cy; p.hw = hw; p.hh = hh; p.rot = rot; p.alpha = alpha;
		p.type = PRIM_RECT; p.fill = FILL_VIDEO_CARD;
		p.c0 = ( rnd[ ( i * 3 ) % 64 ] - 0.5f ) * 0.3f;
		p.c1 = ( rnd[ ( i * 3 + 1 ) % 64 ] - 0.5f ) * 0.3f;
		p.c2 = 1.35f + rnd[ ( i * 3 + 2 ) % 64 ] * 0.6f;
		p.c3 = 0.0f;
		p.shadow = s.shadow; p.radius = 0.004f;
		Add( p );
	};
	auto full = [ & ]( int fill, float zoom, float dim, float alpha ) {
		Prim p{};
		p.cx = a * 0.5f; p.cy = 0.5f; p.hw = a * 0.5f + 0.002f; p.hh = 0.502f; p.alpha = alpha;
		p.type = PRIM_RECT; p.fill = (float)fill; p.c2 = zoom; p.c3 = dim;
		Add( p );
	};

	switch( scene )
	{
	case SC_WORD_REVEAL:
		BuildWord( s, t, D, a, FILL_VIDEO_SCREEN, exitFade, wordY, 1.0f, COL_TEXT, s.tint, true );
		break;
	case SC_WORD_KNOCKOUT:
		full( FILL_VIDEO_CARD, 1.0f + 0.08f * ( 1.0f - easeOutCubic( u * 2.5f ) ), 0.75f, exitFade );
		BuildWord( s, t, D, a, FILL_VIDEO_SCREEN, exitFade, wordY, 1.15f, COL_TEXT, s.tint * 0.35f, true );
		break;
	case SC_FULL_FRAME:
		full( FILL_VIDEO_CARD, 1.0f + 0.12f * ( 1.0f - easeOutCubic( u / 0.35f ) ), 0.0f, 1.0f );
		break;
	case SC_CARD_CASCADE:
	{
		const int n = 4;
		float hh = 0.17f, hw = std::min( hh * a * 0.95f, a * 0.2f );
		for( int i = 0; i < n; ++i )
		{
			float f  = (float)i / ( n - 1 );
			float fx = a * ( 0.2f + 0.6f * f ), fy = 0.22f + 0.56f * f;
			float l  = ( u - i * 0.12f ) / 0.3f;
			if( l <= 0.0f ) continue;
			float e  = easeOutBack( l );
			float sx = fx - a * 0.25f, sy = fy - 0.45f;
			float sc = 0.85f + 0.15f * e;
			card( sx + ( fx - sx ) * e, sy + ( fy - sy ) * e, hw * sc, hh * sc, i, clamp01( l * 4.0f ), 0.0f );
		}
		break;
	}
	case SC_CARD_ZOOM:
	{
		float pop = easeOutBack( u / 0.15f );
		float e   = easeInOutCubic( ( u - 0.35f ) / 0.4f );
		float hh  = ( 0.13f + ( 0.5f - 0.13f ) * e ) * pop;
		Prim p{};
		p.cx = a * 0.5f; p.cy = 0.5f; p.hh = hh + 0.002f * e; p.hw = hh * a + 0.002f * e;
		p.alpha = pop > 0.0f ? 1.0f : 0.0f; p.type = PRIM_RECT; p.fill = FILL_VIDEO_CARD;
		p.c2 = 1.0f; p.shadow = s.shadow * ( 1.0f - e ); p.radius = 0.006f * ( 1.0f - e );
		Add( p );
		break;
	}
	case SC_CARD_SCATTER:
	{
		int n = 3 + (int)( rnd[ 0 ] * 3.0f ) % 3;
		for( int i = 0; i < n; ++i )
		{
			float l = ( u - i * 0.13f ) / 0.22f;
			if( l <= 0.0f ) continue;
			float sc = easeOutBack( l );
			float hh = 0.11f + 0.1f * rnd[ 20 + i ];
			float cx = a * ( 0.2f + 0.6f * rnd[ 1 + i ] ) + ( rnd[ 40 + i ] - 0.5f ) * 0.04f * u;
			float cy = 0.22f + 0.56f * rnd[ 10 + i ] + ( rnd[ 50 + i ] - 0.5f ) * 0.04f * u;
			card( cx, cy, hh * 1.5f * sc, hh * sc, i + 5, 1.0f, ( rnd[ 30 + i ] - 0.5f ) * 0.18f );
		}
		break;
	}
	case SC_IRIS:
	{
		float e    = easeOutExpo( u / 0.55f );
		float maxR = std::sqrt( a * a * 0.25f + 0.25f ) + 0.03f;
		float R    = e * maxR;
		float ring = 1.0f - clamp01( ( u - 0.55f ) / 0.15f );
		Prim p{};
		p.cx = a * 0.5f; p.cy = 0.5f; p.type = PRIM_CIRCLE;
		p.hw = p.hh = R + 0.012f; p.alpha = ring; p.fill = FILL_SOLID; p.color = COL_CREAM;
		if( R > 0.0f ) Add( p );
		p.hw = p.hh = R; p.alpha = 1.0f; p.fill = FILL_VIDEO_SCREEN;
		if( R > 0.0f ) Add( p );
		break;
	}
	case SC_BLINDS:
	{
		const int n = 8;
		float w = a / n;
		for( int i = 0; i < n; ++i )
		{
			float l = easeOutCubic( ( u - i * 0.035f ) / 0.28f );
			if( l <= 0.0f ) continue;
			float from = ( i % 2 ) ? -0.5f : 1.5f;
			Prim p{};
			p.cx = w * ( i + 0.5f ); p.cy = from + ( 0.5f - from ) * l; p.hw = w * 0.5f + 0.001f; p.hh = 0.5f;
			p.alpha = 1.0f; p.type = PRIM_RECT; p.fill = FILL_VIDEO_SCREEN;
			Add( p );
		}
		break;
	}
	case SC_GRID:
	{
		const float gap = 0.012f;
		float tw = ( a - 4.0f * gap ) / 3.0f, th = ( 1.0f - 4.0f * gap ) / 3.0f;
		int order[ 9 ] = { 0, 1, 2, 3, 4, 5, 6, 7, 8 };
		for( int i = 8; i > 0; --i ) std::swap( order[ i ], order[ (int)( rnd[ i ] * ( i + 1 ) ) % ( i + 1 ) ] );
		for( int r = 0; r < 9; ++r )
		{
			int cell = order[ r ];
			float l  = ( u - r / 9.0f * 0.4f ) / 0.2f;
			if( l <= 0.0f ) continue;
			float sc = easeOutBack( l );
			int cx = cell % 3, cy = cell / 3;
			Prim p{};
			p.cx = gap + tw * 0.5f + cx * ( tw + gap ); p.cy = gap + th * 0.5f + cy * ( th + gap );
			p.hw = tw * 0.5f * sc; p.hh = th * 0.5f * sc; p.alpha = 1.0f; p.type = PRIM_RECT;
			p.fill = FILL_VIDEO_SCREEN; p.shadow = s.shadow * 0.6f; p.radius = 0.003f;
			Add( p );
		}
		break;
	}
	}

	// optional caption on every non-word scene (like the "PHOTO 10" labels)
	if( s.overlay == 1 && scene != SC_WORD_REVEAL && scene != SC_WORD_KNOCKOUT )
		BuildWord( s, t, D, a, FILL_SOLID, exitFade, 0.5f, 0.42f, COL_CREAM, 1.0f, false );

	// flashes on the cut
	if( flashType >= 0 )
	{
		Prim p{};
		p.type = PRIM_RECT;
		if( flashType == 0 && tReal < 0.12f )
		{
			p.hh = 0.1f + 0.06f * rnd[ 42 ]; p.hw = p.hh * 1.6f;
			p.cx = a * ( 0.25f + 0.5f * rnd[ 43 ] ); p.cy = 0.25f + 0.5f * rnd[ 44 ];
			p.alpha = 1.0f; p.fill = FILL_SOLID; p.color = COL_CREAM;
			Add( p );
		}
		else if( flashType == 1 && tReal < 0.18f )
		{
			p.cx = a * 0.5f; p.cy = 0.5f; p.hw = a * 0.5f + 0.01f; p.hh = 0.51f;
			p.alpha = 1.0f - tReal / 0.18f; p.fill = FILL_SOLID; p.color = COL_WHITE;
			Add( p );
		}
		else if( flashType == 2 && tReal < 0.3f )
		{
			p.cx = a * 0.5f; p.cy = 0.5f; p.hw = a * 0.5f + 0.01f; p.hh = 0.51f;
			p.alpha = 0.9f * ( 1.0f - tReal / 0.3f ); p.fill = FILL_LEAK;
			p.c0 = rnd[ 45 ]; p.c1 = rnd[ 46 ];
			Add( p );
		}
	}
}
}//namespace cutup
