#pragma once

#ifndef __SuperLFO__
#define __SuperLFO__

#include "fxobjects.h"

// --- LFO may have very diff waveforms from pitched output
enum class LFOWaveform { kTriangle, kSin, kSaw, kRSH, kQRSH, kNoise, kQRNoise };
enum class LFOMode { kSync, kOneShot, kFreeRun };

/**
\struct SuperLFOParameters
\ingroup FX-Objects
\brief
Custom parameter structure for the SuperLFO object.

\author <Your Name> <http://www.yourwebsite.com>
\remark <Put any remarks or notes here>
\version Revision : 1.0
\date Date : 2019 / 01 / 31
*/
struct SuperLFOParameters
{
	SuperLFOParameters() {}

	/** all FXObjects parameter objects require overloaded= operator so remember to add new entries if you add new variables. */
	SuperLFOParameters& operator=(const SuperLFOParameters& params)	// need this override for collections to work
	{
		// --- it is possible to try to make the object equal to itself
		//     e.g. thisObject = thisObject; so this code catches that
		//     trivial case and just returns this object
		if (this == &params)
			return *this;

		// --- copy from params (argument) INTO our variables
		waveform = params.waveform;
		mode = params.mode;
		frequency_Hz = params.frequency_Hz;
		outputAmplitude = params.outputAmplitude;


		// --- MUST be last
		return *this;
	}

	// --- individual parameters
	LFOWaveform waveform = LFOWaveform::kTriangle;
	LFOMode mode = LFOMode::kSync;

	double frequency_Hz = 0.0;
	double outputAmplitude = 1.0;
};

struct SignalModulatorOutput
{
	SignalModulatorOutput() {}

	double normalOutput = 0.0;			///< normal
	double invertedOutput = 0.0;		///< inverted
	double quadPhaseOutput_pos = 0.0;	///< 90 degrees out
	double quadPhaseOutput_neg = 0.0;	///< -90 degrees out
	double unipolarOutputFromMax = 1.0;	///< top down modulation
	double unipolarOutputFromMin = 0.0;	///< bottom up modulation
	double quadPhaseUnipolarOutputFromMax = 1.0;	///< top down modulation
	double quadPhaseUnipolarOutputFromMin = 0.0;	///< bottom up modulation
};

/**
\class IAudioSignalGenerator
\ingroup Interfaces
\brief
Use this interface for objects that render an output without an input, such as oscillators. May also be used for envelope generators whose input is a note-on or other switchable event.

Outpput I/F:
- Use SignalGenData structure for output.

\author Will Pirkle http://www.willpirkle.com
\remark This object is included in Designing Audio Effects Plugins in C++ 2nd Ed. by Will Pirkle
\version Revision : 1.0
\date Date : 2018 / 09 / 7
*/
class IAudioSignalModualtor
{
public:
	// --- pure virtual, derived classes must implement or will not compile
	//     also means this is a pure abstract base class and is incomplete,
	//     so it can only be used as a base class
	//
	/** Sample rate may or may not be required, but usually is */
	virtual bool reset(double _sampleRate) = 0;

	/** render the generator output */
	virtual const SignalModulatorOutput renderModulatorOutput() = 0;
};

/**
\class SuperLFO
\ingroup FX-Objects
\brief
The SuperLFO object implements ....

Audio I/O:
- Processes mono input to mono output.
- *** Optionally, process frame *** Modify this according to your object functionality

Control I/F:
- Use SuperLFOParameters structure to get/set object params.

\author <Your Name> <http://www.yourwebsite.com>
\remark <Put any remarks or notes here>
\version Revision : 1.0
\date Date : 2019 / 01 / 31
*/
class SuperLFO : public IAudioSignalModualtor
{
public:
	SuperLFO(void) {}	/* C-TOR */
	~SuperLFO(void) {}	/* D-TOR */

public:
	/** reset members to initialized state */
	virtual bool reset(double _sampleRate)
	{
		// --- do any other per-audio-run inits here
		sampleRate = _sampleRate;

		srand(time(NULL)); // --- seed random number generator

		// --- randomize the PN register
		pnRegister = rand();

		// --- calculate modulo counter phase incrementer
		phaseInc = parameters.frequency_Hz / sampleRate;

		// --- timebase variables
		modCounter = 0.0;			///< modulo counter [0.0, +1.0]
		modCounterQP = 0.25;		///<Quad Phase modulo counter [0.0, +1.0]

		return true;
	}

	/** process MONO input */
	/**
	\param xn input
	\return the processed sample
	*/
	virtual const SignalModulatorOutput renderModulatorOutput()
	{
		// --- setup for output
		SignalModulatorOutput lfoOutputData; // should auto-zero on instantiation

		if (renderComplete)
		{
			return lfoOutputData;
		}

		// --- always first!
		bool bWrapped = checkAndWrapModulo(modCounter, phaseInc);
		if (bWrapped && parameters.mode == LFOMode::kOneShot)
		{
			renderComplete = true;
			return lfoOutputData;
		}

		// --- QP output always follows location of current modulo; first set equal
		modCounterQP = modCounter;

		// --- then, advance modulo by quadPhaseInc = 0.25 = 90 degrees, AND wrap if needed
		advanceAndCheckWrapModulo(modCounterQP, 0.25);

		// --- calculate the oscillator value
		if (parameters.waveform == LFOWaveform::kSin)
		{
			// --- calculate normal angle
			double angle = modCounter*2.0*kPi - kPi;

			// --- norm output with parabolicSine approximation
			lfoOutputData.normalOutput = parabolicSine(-angle);

			// --- calculate QP angle
			angle = modCounterQP*2.0*kPi - kPi;

			// --- calc QP output
			lfoOutputData.quadPhaseOutput_pos = parabolicSine(-angle);
		}
		else if (parameters.waveform == LFOWaveform::kTriangle)
		{
			// --- triv saw
			lfoOutputData.normalOutput = unipolarToBipolar(modCounter);

			// --- bipolar triagle
			lfoOutputData.normalOutput = 2.0*fabs(lfoOutputData.normalOutput) - 1.0;

			// -- quad phase
			lfoOutputData.quadPhaseOutput_pos = unipolarToBipolar(modCounterQP);

			// bipolar triagle
			lfoOutputData.quadPhaseOutput_pos = 2.0*fabs(lfoOutputData.quadPhaseOutput_pos) - 1.0;
		}
		else if (parameters.waveform == LFOWaveform::kSaw)
		{
			lfoOutputData.normalOutput = unipolarToBipolar(modCounter);
			lfoOutputData.quadPhaseOutput_pos = unipolarToBipolar(modCounterQP);
		}
		else if (parameters.waveform == LFOWaveform::kNoise)
		{
			lfoOutputData.normalOutput = doWhiteNoise();
			lfoOutputData.quadPhaseOutput_pos = doWhiteNoise();
		}
		else if (parameters.waveform == LFOWaveform::kQRNoise)
		{
			lfoOutputData.normalOutput = doPNSequence(pnRegister);
			lfoOutputData.quadPhaseOutput_pos = doPNSequence(pnRegister);
		}
		else if (parameters.waveform == LFOWaveform::kRSH || parameters.waveform == LFOWaveform::kQRSH)
		{
			// --- is this is the very first run? if so, form first output sample
			if (randomSHCounter < 0)
			{
				if (parameters.waveform == LFOWaveform::kRSH)
					randomSHValue = doWhiteNoise();
				else
					randomSHValue = doPNSequence(pnRegister);

				// --- init the sample counter, will be advanced below
				randomSHCounter = 1.0;
			}
			// --- has hold time been exceeded? if so, generate next output sample
			else if (randomSHCounter > (sampleRate / parameters.frequency_Hz))
			{
				// --- wrap counter
				randomSHCounter -= sampleRate / parameters.frequency_Hz;

				if (parameters.waveform == LFOWaveform::kRSH)
					randomSHValue = doWhiteNoise();
				else
					randomSHValue = doPNSequence(pnRegister);
			}

			// --- advance the sample counter
			randomSHCounter += 1.0;

			lfoOutputData.normalOutput = randomSHValue;
			lfoOutputData.quadPhaseOutput_pos = randomSHValue;
		}

		// --- scale by amplitude
		lfoOutputData.normalOutput *= parameters.outputAmplitude;
		lfoOutputData.quadPhaseOutput_pos *= parameters.outputAmplitude;

		// --- invert two main outputs to make the opposite versions, scaling carries over
		lfoOutputData.invertedOutput = -lfoOutputData.normalOutput;
		lfoOutputData.quadPhaseOutput_neg = -lfoOutputData.quadPhaseOutput_pos;

		// --- special unipolar from max output for tremolo
		//
		// --- first, convert to unipolar
		lfoOutputData.unipolarOutputFromMax = bipolarToUnipolar(lfoOutputData.normalOutput);
		lfoOutputData.unipolarOutputFromMin = bipolarToUnipolar(lfoOutputData.normalOutput);
		lfoOutputData.quadPhaseUnipolarOutputFromMax = bipolarToUnipolar(lfoOutputData.quadPhaseOutput_pos);
		lfoOutputData.quadPhaseUnipolarOutputFromMin = bipolarToUnipolar(lfoOutputData.quadPhaseOutput_pos);

		// --- then shift upwards by enough to put peaks right at 1.0
		//     NOTE: leaving the 0.5 in the equation - it is the unipolar offset when convering bipolar; but it could be changed...
		lfoOutputData.unipolarOutputFromMax = lfoOutputData.unipolarOutputFromMax + (1.0 - 0.5 - (parameters.outputAmplitude / 2.0));
		lfoOutputData.quadPhaseUnipolarOutputFromMax = lfoOutputData.quadPhaseUnipolarOutputFromMax + (1.0 - 0.5 - (parameters.outputAmplitude / 2.0));

		// --- then shift down enough to put troughs at 0.0
		lfoOutputData.unipolarOutputFromMin = lfoOutputData.unipolarOutputFromMin - (1.0 - 0.5 - (parameters.outputAmplitude / 2.0));
		lfoOutputData.quadPhaseUnipolarOutputFromMin = lfoOutputData.quadPhaseUnipolarOutputFromMin - (1.0 - 0.5 - (parameters.outputAmplitude / 2.0));

		// --- setup for next sample period
		advanceModulo(modCounter, phaseInc);

		// --- scale by amplituded
		return lfoOutputData;
	}


	/** get parameters: note use of custom structure for passing param data */
	/**
	\return SuperLFOParameters custom data structure
	*/
	SuperLFOParameters getParameters()
	{
		return parameters;
	}

	/** set parameters: note use of custom structure for passing param data */
	/**
	\param SuperLFOParameters custom data structure
	*/
	void setParameters(const SuperLFOParameters& params)
	{
		// --- copy them; note you may choose to ignore certain items
		//     and copy the variables one at a time, or you may test
		//     to see if cook-able variables have changed; if not, then
		//     do not re-cook them as it just wastes CPU
		parameters = params;

		// --- cook parameters here
		phaseInc = parameters.frequency_Hz / sampleRate;
	}

private:
	SuperLFOParameters parameters; ///< object parameters

	// --- local variables used by this object
	double sampleRate = 0.0;	///< sample rate
								
	// --- timebase variables
	double modCounter = 0.0;			///< modulo counter [0.0, +1.0]
	double phaseInc = 0.0;				///< phase inc = fo/fs
	double modCounterQP = 0.25;			///< Quad Phase modulo counter [0.0, +1.0]
	bool renderComplete = false;		///< flag for one-shot

	// --- 32-bit register for RS&H
	uint32_t pnRegister = 0;			///< 32 bit register for PN oscillator
	int randomSHCounter = -1;			///< random sample/hold counter;  -1 is reset condition
	double randomSHValue = 0.0;			///< current output, needed because we hold this output for some number of samples = (sampleRate / oscFrequency)

	/**
	\struct checkAndWrapModulo
	\brief Check a modulo counter and wrap it if necessary
	*/
	inline bool checkAndWrapModulo(double& moduloCounter, double phaseInc)
	{
		// --- for positive frequencies
		if (phaseInc > 0 && moduloCounter >= 1.0)
		{
			moduloCounter -= 1.0;
			return true;
		}

		// --- for negative frequencies
		if (phaseInc < 0 && moduloCounter <= 0.0)
		{
			moduloCounter += 1.0;
			return true;
		}

		return false;
	}

	/**
	\struct checkAndWrapModulo
	\brief Advance, and then check a modulo counter and wrap it if necessary
	*/
	inline bool advanceAndCheckWrapModulo(double& moduloCounter, double phaseInc)
	{
		// --- advance counter
		moduloCounter += phaseInc;

		// --- for positive frequencies
		if (phaseInc > 0 && moduloCounter >= 1.0)
		{
			moduloCounter -= 1.0;
			return true;
		}

		// --- for negative frequencies
		if (phaseInc < 0 && moduloCounter <= 0.0)
		{
			moduloCounter += 1.0;
			return true;
		}

		return false;
	}

	// --- increment the modulo counter
	inline void advanceModulo(double& moduloCounter, double phaseInc) { moduloCounter += phaseInc; }

	// --- sine approximation with parabolas
	const double B = 4.0 / kPi;
	const double C = -4.0 / (kPi* kPi);
	const double P = 0.225;
	// http://devmaster.net/posts/9648/fast-and-accurate-sine-cosine
	//
	// --- angle is -pi to +pi
	inline double parabolicSine(double angle)
	{
		double y = B * angle + C * angle * fabs(angle);
		y = P * (y * fabs(y) - y) + y;
		return y;
	}

	#define EXTRACT_BITS(the_val, bits_start, bits_len) ((the_val >> (bits_start - 1)) & ((1 << bits_len) - 1))

	// --- quasi-random noise gnerator
	inline double doPNSequence(uint32_t& uPNRegister)
	{
		// --- get the bits
		uint32_t b0 = EXTRACT_BITS(uPNRegister, 1, 1); // 1 = b0 is FIRST bit from right
		uint32_t b1 = EXTRACT_BITS(uPNRegister, 2, 1); // 1 = b1 is SECOND bit from right
		uint32_t b27 = EXTRACT_BITS(uPNRegister, 28, 1); // 28 = b27 is 28th bit from right
		uint32_t b28 = EXTRACT_BITS(uPNRegister, 29, 1); // 29 = b28 is 29th bit from right

		// --- form the XOR
		uint32_t b31 = b0^b1^b27^b28;

		// --- form the mask to OR with the register to load b31
		if (b31 == 1)
			b31 = 0x10000000;

		// --- shift one bit to right
		uPNRegister >>= 1;

		// --- set the b31 bit
		uPNRegister |= b31;

		// --- convert the output into a floating point number, scaled by experimentation
		// --- to a range of o to +2.0
		float fOut = (float)(uPNRegister) / ((pow((float)2.0, (float)32.0)) / 16.0);

		// --- shift down to form a result from -1.0 to +1.0
		fOut -= 1.0;

		return fOut;
	}
};

#endif