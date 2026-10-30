#pragma once
// CUTUP scene engine - plain C++ (no OpenGL), so the exact same logic runs in the plugin
// and in tools/cutup_sim.cpp for previews.
#include <string>
#include <vector>
#include <random>

namespace cutup
{
static const int kMaxPrims  = 160;
static const int kTextCount = 16;
static const int kSceneCount = 9;

enum PrimType { PRIM_RECT = 0, PRIM_CIRCLE = 1, PRIM_GLYPH = 2 };
enum Fill { FILL_VIDEO_SCREEN = 0, FILL_VIDEO_CARD = 1, FILL_SOLID = 2, FILL_LEAK = 3 };
enum SolidColor { COL_TEXT = 0, COL_CREAM = 1, COL_WHITE = 2, COL_BLACK = 3 };

// One drawable. Coordinates are in "frame space": x in [0, aspect], y in [0, 1], y up.
struct Prim
{
	float cx, cy, hw, hh;     // centre, half size
	float rot, alpha, type, fill;
	float c0, c1, c2, c3;     // glyph: atlas u0 v0 u1 v1 | card: content offset x/y, zoom, dim
	float shadow, tint, radius, color;
};

enum Scene
{
	SC_WORD_REVEAL, SC_WORD_KNOCKOUT, SC_FULL_FRAME, SC_CARD_CASCADE, SC_CARD_ZOOM,
	SC_CARD_SCATTER, SC_IRIS, SC_BLINDS, SC_GRID
};
extern const char* kSceneNames[ kSceneCount ];

struct Settings
{
	bool autoBeat      = true;
	float divisionBeats = 1.0f;
	int sceneOrder     = 2;  // 0 sequential, 1 random, 2 shuffle
	int textOrder      = 0;  // 0 sequential, 1 random, 2 shuffle, 3 ping-pong
	int reveal         = 0;  // 0 random pop, 1 left->right, 2 slide up, 3 scramble, 4 all at once
	int font           = 0;
	float textSize     = 0.5f;
	int textPos        = 1;  // 0 centre, 1 random, 2 top/mid/bottom cycle
	float tint         = 0.55f;
	bool uppercase     = true;
	int overlay        = 0;  // 0 off, 1 label on every scene
	float flash        = 0.35f;
	float speed        = 0.5f;
	float shadow       = 0.6f;
	bool sceneOn[ kSceneCount ] = { true, true, true, true, true, true, true, true, true };
	std::string texts[ kTextCount ];
};

class Engine
{
public:
	Engine();
	// Call once per frame. timeSec: monotonic seconds. barPhase: host bar phase 0..1 (or <0 if unknown).
	void Update( double timeSec, float bpm, float barPhase, float aspect, const Settings& s );
	void Trigger(); // "Next" button: advance on the next Update
	const std::vector< Prim >& Prims() const { return prims; }
	int CurrentScene() const { return scene; }
	const std::u32string& CurrentWord() const { return word; }

private:
	void StartScene( const Settings& s, double now, float aspect );
	int PickScene( const Settings& s );
	void PickWord( const Settings& s );
	float Rand() { return dist( rng ); }

	void BuildWord( const Settings& s, float t, float D, float aspect, int fill, float alphaMul, float y, float sizeMul, int colorIdx, float tint, bool animate );
	void Add( const Prim& p ) { if( (int)prims.size() < kMaxPrims ) prims.push_back( p ); }

	std::mt19937 rng;
	std::uniform_real_distribution< float > dist;
	std::vector< Prim > prims;

	bool pendingTrigger = true;
	double beatTotal    = 0.0;
	float lastBarPos    = -1.0f;
	double lastTime     = -1.0;
	long long lastSlice = -1;

	int scene        = -1;
	double sceneStart = 0.0;
	double interval   = 0.5;
	std::vector< int > sceneBag;
	int seqScene     = -1;

	std::u32string word;
	int wordIndex    = -1;
	int pingDir      = 1;
	std::vector< int > textBag;
	int posCycle     = 0;
	float wordY      = 0.5f;
	std::vector< float > letterDelay;

	// per-scene randomness
	float rnd[ 64 ];
	int flashType    = -1;
};

std::u32string Utf8ToU32( const std::string& s );
}//namespace cutup
