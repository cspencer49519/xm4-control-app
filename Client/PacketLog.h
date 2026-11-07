#pragma once

// Protocol debug logging. Enabled by setting the SONY_HEADPHONES_DEBUG environment
// variable; prints every packet in both directions to stderr with a timestamp
// relative to process start, e.g.
//   [   12.345] -> MDR  seq=1  66 02
//   [   12.401] <- ACK  seq=0
//   [   12.420] <- MDR  seq=0  67 02 01 02 02 01 00 00

#include "Constants.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>

namespace PacketLog
{
	inline bool enabled()
	{
		static const bool on = std::getenv("SONY_HEADPHONES_DEBUG") != nullptr;
		return on;
	}

	inline const char* typeName(DATA_TYPE type)
	{
		switch (type)
		{
		case DATA_TYPE::ACK: return "ACK ";
		case DATA_TYPE::DATA_MDR: return "MDR ";
		case DATA_TYPE::DATA_MDR_NO2: return "MDR2";
		case DATA_TYPE::DATA_COMMON: return "COMM";
		default: return "????";
		}
	}

	inline void log(bool outgoing, DATA_TYPE type, unsigned int seq, const Buffer& payload, const char* note = nullptr)
	{
		if (!enabled())
			return;
		static const auto start = std::chrono::steady_clock::now();
		static std::mutex mtx;
		std::lock_guard guard(mtx);
		double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		std::fprintf(stderr, "[%9.3f] %s %s seq=%u ", t, outgoing ? "->" : "<-", typeName(type), seq & 0xff);
		if (static_cast<int>(type) != 0 && typeName(type)[0] == '?')
			std::fprintf(stderr, "(type %d) ", static_cast<int>(type));
		for (char c : payload)
			std::fprintf(stderr, " %02x", static_cast<unsigned char>(c));
		if (note)
			std::fprintf(stderr, "  # %s", note);
		std::fprintf(stderr, "\n");
	}

	inline void note(const char* text)
	{
		if (!enabled())
			return;
		std::fprintf(stderr, "[   note  ] %s\n", text);
	}
}
