#pragma once
// PUMP envelope engine - plain C++ (no OpenGL) so the plugin and tools/pump_sim.cpp share it exactly.
// Produces "hits" from the beat clock (with rhythm patterns), from the audio kick, or from the button,
// and turns them into a 0..1 envelope that drives every visual target.
#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

namespace pump
{
enum Source { SRC_BEAT = 0, SRC_AUDIO = 1, SRC_BEAT_AND_AUDIO = 2, SRC_MANUAL = 3 };
enum Pattern { PAT_BEAT, PAT_OFFBEAT, PAT_ONE_THREE, PAT_BACKBEAT, PAT_BAR, PAT_EIGHTHS, PAT_SIXTEENTHS, PAT_BUILDUP, PAT_RANDOM, PAT_COUNT };
enum Style { STY_PUNCH = 0, STY_SIDECHAIN = 1, STY_SWELL = 2, STY_STROBE = 3 };

static const char* const kSourceNames[ 4 ]          = { "Beat Clock", "Audio Kick", "Beat + Audio", "Button Only" };
static const char* const kPatternNames[ PAT_COUNT ] = { "Every Beat", "Off-Beats", "Beats 1 & 3", "Backbeat 2 & 4", "Bar (1)", "8ths", "16ths", "Build-Up (4 bars)", "Random 8ths" };
static const char* const kStyleNames[ 4 ]           = { "Punch", "Sidechain Duck", "Swell", "Strobe" };

struct Settings
{
	int source      = SRC_BEAT;
	int pattern     = PAT_BEAT;
	int style       = STY_PUNCH;
	float decay     = 0.55f; // 0..1 -> fraction of a beat the hit lasts
	float curve     = 0.35f; // 0 = linear fade, 1 = very snappy exponential
	float attack    = 0.0f;  // 0..1 -> 0..60 ms soft attack
	float accent    = 0.6f;  // how much weaker non-downbeat hits are (0 = all equal)
	float sensitivity = 0.6f;// audio kick sensitivity
};

struct State
{
	float env      = 0.0f; // 0..1 final envelope (after style)
	float raw      = 0.0f; // 0..1 decaying hit value (before style)
	float accentMul = 1.0f;
	float sinceHit = 10.0f; // seconds since the last hit
	float shakeX = 0.0f, shakeY = 0.0f;
	float hitCount = 0.0f;
	float beatPhase = 0.0f; // 0..1 within the current beat
};

class Envelope
{
public:
	Envelope() : rng( 77u ), dist( 0.0f, 1.0f ) {}

	void Trigger() { manualPending = true; }

	// fft: optional spectrum (Resolume sends 0..1 magnitudes, low bins first); sampleRate for bin->Hz.
	const State& Update( double timeSec, float bpm, float barPhase, bool hostBeat, const Settings& s,
	                     const float* fft = nullptr, int fftSize = 0, float sampleRate = 44100.0f )
	{
		if( bpm < 20.0f ) bpm = 120.0f;
		if( lastTime < 0.0 ) lastTime = timeSec;
		double dt = std::max( 0.0, timeSec - lastTime );
		lastTime  = timeSec;
		float beatSec = 60.0f / bpm;

		// --- position inside the bar (16 ticks per bar)
		double barPos;
		if( hostBeat ) barPos = barPhase * 4.0;
		else { internalBeats += dt / beatSec; barPos = std::fmod( internalBeats, 4.0 ); }
		bool wrapped = lastBarPos >= 0.0 && barPos + 1e-6 < lastBarPos - 2.0;
		if( wrapped ) barCount++;
		lastBarPos      = barPos;
		st.beatPhase    = (float)( barPos - std::floor( barPos ) );
		int tick        = (int)std::floor( barPos * 4.0 ) % 16;

		bool hit = false; float hitAccent = 1.0f;
		bool useBeat  = s.source == SRC_BEAT || s.source == SRC_BEAT_AND_AUDIO;
		bool useAudio = s.source == SRC_AUDIO || s.source == SRC_BEAT_AND_AUDIO;

		if( tick != lastTick && useBeat )
		{
			if( PatternHit( s.pattern, tick, barCount ) )
			{
				hit       = true;
				hitAccent = tick == 0 ? 1.0f : 1.0f - 0.45f * s.accent;
			}
		}
		lastTick = tick;

		// --- audio kick: onset on the low-frequency energy
		if( useAudio && fft && fftSize > 4 )
		{
			float binHz = sampleRate * 0.5f / (float)fftSize;
			int hi      = std::max( 2, std::min( fftSize, (int)( 150.0f / std::max( binHz, 1.0f ) ) + 1 ) );
			float bass  = 0.0f;
			for( int i = 1; i < hi; ++i ) bass += fft[ i ];
			bass /= (float)( hi - 1 );
			avgBass        = avgBass * 0.97f + bass * 0.03f;
			float thresh   = avgBass * ( 1.15f + ( 1.0f - s.sensitivity ) * 1.6f ) + 0.01f;
			bool rising    = bass > lastBass;
			if( bass > thresh && rising && timeSec - lastAudioHit > 0.12 )
			{
				hit          = true;
				hitAccent    = std::min( 1.0f, 0.55f + ( bass / std::max( avgBass, 1e-4f ) - 1.0f ) * 0.5f );
				lastAudioHit = timeSec;
			}
			lastBass = bass;
		}
		if( manualPending ) { hit = true; hitAccent = 1.0f; manualPending = false; }

		if( hit )
		{
			lastHit       = timeSec;
			st.accentMul  = hitAccent;
			st.hitCount  += 1.0f;
			st.shakeX     = dist( rng ) * 2.0f - 1.0f;
			st.shakeY     = dist( rng ) * 2.0f - 1.0f;
		}

		// --- envelope
		st.sinceHit   = (float)( timeSec - lastHit );
		float decaySec = std::max( 0.03f, ( 0.08f + s.decay * 1.4f ) * beatSec );
		float x        = st.sinceHit / decaySec;
		float k        = 1.0f + s.curve * 7.0f;
		float raw      = x >= 1.0f ? 0.0f : ( std::exp( -k * x ) - std::exp( -k ) ) / ( 1.0f - std::exp( -k ) );
		float att      = s.attack * 0.06f;
		if( att > 0.0f ) raw *= std::min( 1.0f, st.sinceHit / att );
		raw *= st.accentMul;
		st.raw = raw;

		switch( s.style )
		{
		case STY_SIDECHAIN: st.env = 1.0f - raw; break;
		case STY_SWELL:
		{
			// rises towards the next hit, then cuts: uses the beat phase for clock sources
			float ph = s.pattern == PAT_BAR ? (float)( barPos / 4.0 ) : st.beatPhase;
			st.env   = std::pow( ph, 1.0f + s.curve * 4.0f ) * ( useBeat ? 1.0f : 0.0f ) + ( useBeat ? 0.0f : raw );
			break;
		}
		case STY_STROBE: st.env = ( st.sinceHit < decaySec * 0.5f ) ? st.accentMul : 0.0f; break;
		default: st.env = raw;
		}
		return st;
	}

private:
	bool PatternHit( int pattern, int tick, long long bar )
	{
		switch( pattern )
		{
		case PAT_OFFBEAT:    return tick % 4 == 2;
		case PAT_ONE_THREE:  return tick == 0 || tick == 8;
		case PAT_BACKBEAT:   return tick == 4 || tick == 12;
		case PAT_BAR:        return tick == 0;
		case PAT_EIGHTHS:    return tick % 2 == 0;
		case PAT_SIXTEENTHS: return true;
		case PAT_BUILDUP:
		{
			int b = (int)( ( bar % 4 + 4 ) % 4 );
			if( b < 2 ) return tick % 4 == 0;
			if( b == 2 ) return tick % 2 == 0;
			return true;
		}
		case PAT_RANDOM:     return tick % 2 == 0 && ( tick == 0 || dist( rng ) < 0.5f );
		default:             return tick % 4 == 0;
		}
	}

	State st;
	std::mt19937 rng;
	std::uniform_real_distribution< float > dist;
	double lastTime = -1.0, internalBeats = 0.0, lastBarPos = -1.0, lastHit = -10.0, lastAudioHit = -10.0;
	long long barCount = 0;
	int lastTick = -1;
	bool manualPending = false;
	float avgBass = 0.0f, lastBass = 0.0f;
};
}//namespace pump
