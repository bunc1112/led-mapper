#pragma once
#include <chrono>
#include <random>
#include <FFGLSDK.h>

// CHRONOS - twenty time-bending effects in one native FFGL plugin.
// Keeps a GPU ring buffer of past frames (GL_TEXTURE_2D_ARRAY) and lets every mode read from it.
static const int kModeCount = 20;

class Chronos : public CFFGLPlugin
{
public:
	Chronos();
	~Chronos() override;

	FFResult InitGL( const FFGLViewportStruct* vp ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* pGL ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;
	char* GetParameterDisplay( unsigned int index ) override;

private:
	bool EnsureHistory( GLsizei width, GLsizei height );
	void ReleaseHistory();
	void UpdateBeat();
	void ApplyModeUI( bool raiseEvents );
	void ClearState();
	static bool IsStateful( int m ) { return m == 8 || m == 10 || m == 11; }

	ffglex::FFGLShader mainShader;
	ffglex::FFGLShader copyShader;
	ffglex::FFGLShader stateShader;
	ffglex::FFGLScreenQuad quad;

	GLuint historyTex = 0;
	GLuint fbo        = 0;
	GLuint stateTex[ 2 ] = { 0, 0 };
	int stateCur         = 0;
	bool stateDirty      = true;
	bool refreshPending  = true;
	GLsizei histW = 0, histH = 0, histN = 0;
	int head  = -1;
	int valid = 0;

	// parameters
	int mode        = 0;
	int timeMap     = 0;
	float depth     = 0.6f;
	float paramA    = 0.0f;
	float paramB    = 0.5f;
	bool reverse    = false;
	int division    = 2;
	bool freeze     = false;
	float mixAmount = 1.0f;
	int framesOpt   = 2;
	int resOpt      = 1;
	float paramStore[ kModeCount ][ 2 ];// remembers A/B per mode so switching modes feels like switching effects

	// beat / stutter state
	std::chrono::steady_clock::time_point startTime;
	std::mt19937 rng;
	unsigned long long frameCounter = 0;
	double lastBeatPos             = -1.0;
	float lastBarPhase             = 0.0f;
	bool hostBeat                  = false;
	long long sliceIndex           = -1;
	float sliceCounter             = 0.0f;
	bool stuttering                = false;
	unsigned long long sliceStart  = 0;
	int loopLen                    = 8;
	float stutterAge               = 0.0f;
	float beatPhase                = 0.0f;

	char displayBuf[ 32 ];
};
