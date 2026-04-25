#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <propvarutil.h>
#include <wrl/client.h>

#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

static void WriteU16(std::ofstream& Out, uint16_t Value)
{
	Out.put(static_cast<char>(Value & 0xff));
	Out.put(static_cast<char>((Value >> 8) & 0xff));
}

static void WriteU32(std::ofstream& Out, uint32_t Value)
{
	Out.put(static_cast<char>(Value & 0xff));
	Out.put(static_cast<char>((Value >> 8) & 0xff));
	Out.put(static_cast<char>((Value >> 16) & 0xff));
	Out.put(static_cast<char>((Value >> 24) & 0xff));
}

static bool WriteWav(const std::wstring& OutputPath, const std::vector<uint8_t>& Data, uint32_t SampleRate, uint16_t Channels, uint16_t BitsPerSample)
{
	std::ofstream Out(OutputPath, std::ios::binary);
	if (!Out)
	{
		std::wcerr << L"Failed to open output wav: " << OutputPath << L"\n";
		return false;
	}

	const uint16_t BlockAlign = static_cast<uint16_t>(Channels * BitsPerSample / 8);
	const uint32_t ByteRate = SampleRate * BlockAlign;
	const uint32_t DataSize = static_cast<uint32_t>(Data.size());

	Out.write("RIFF", 4);
	WriteU32(Out, 36 + DataSize);
	Out.write("WAVE", 4);
	Out.write("fmt ", 4);
	WriteU32(Out, 16);
	WriteU16(Out, 1);
	WriteU16(Out, Channels);
	WriteU32(Out, SampleRate);
	WriteU32(Out, ByteRate);
	WriteU16(Out, BlockAlign);
	WriteU16(Out, BitsPerSample);
	Out.write("data", 4);
	WriteU32(Out, DataSize);
	Out.write(reinterpret_cast<const char*>(Data.data()), Data.size());
	return true;
}

static bool GetAudioFormat(IMFMediaType* MediaType, uint32_t& SampleRate, uint16_t& Channels, uint16_t& BitsPerSample)
{
	UINT32 Value = 0;
	if (FAILED(MediaType->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &Value)))
	{
		return false;
	}
	SampleRate = Value;

	if (FAILED(MediaType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &Value)))
	{
		return false;
	}
	Channels = static_cast<uint16_t>(Value);

	if (FAILED(MediaType->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &Value)))
	{
		BitsPerSample = 16;
	}
	else
	{
		BitsPerSample = static_cast<uint16_t>(Value);
	}

	return true;
}

int wmain(int Argc, wchar_t** Argv)
{
	if (Argc < 3)
	{
		std::wcerr << L"Usage: mf_decode.exe <input mp3/mp4/m4a/etc> <output wav>\n";
		return 2;
	}

	const std::wstring InputPath = Argv[1];
	const std::wstring OutputPath = Argv[2];

	HRESULT Hr = MFStartup(MF_VERSION);
	if (FAILED(Hr))
	{
		std::wcerr << L"MFStartup failed: 0x" << std::hex << Hr << L"\n";
		return 1;
	}

	ComPtr<IMFSourceReader> Reader;
	Hr = MFCreateSourceReaderFromURL(InputPath.c_str(), nullptr, &Reader);
	if (FAILED(Hr))
	{
		std::wcerr << L"Could not open input with Media Foundation: 0x" << std::hex << Hr << L"\n";
		MFShutdown();
		return 1;
	}

	ComPtr<IMFMediaType> OutputType;
	Hr = MFCreateMediaType(&OutputType);
	if (FAILED(Hr))
	{
		MFShutdown();
		return 1;
	}

	OutputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
	OutputType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
	OutputType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
	Hr = Reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, OutputType.Get());
	if (FAILED(Hr))
	{
		std::wcerr << L"Could not set PCM output type: 0x" << std::hex << Hr << L"\n";
		MFShutdown();
		return 1;
	}

	ComPtr<IMFMediaType> CurrentType;
	Hr = Reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, &CurrentType);
	if (FAILED(Hr))
	{
		std::wcerr << L"Could not query PCM output type: 0x" << std::hex << Hr << L"\n";
		MFShutdown();
		return 1;
	}

	uint32_t SampleRate = 0;
	uint16_t Channels = 0;
	uint16_t BitsPerSample = 16;
	if (!GetAudioFormat(CurrentType.Get(), SampleRate, Channels, BitsPerSample))
	{
		std::wcerr << L"Could not read decoded audio format\n";
		MFShutdown();
		return 1;
	}

	std::vector<uint8_t> PcmData;
	while (true)
	{
		DWORD StreamIndex = 0;
		DWORD Flags = 0;
		LONGLONG Timestamp = 0;
		ComPtr<IMFSample> Sample;
		Hr = Reader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, &StreamIndex, &Flags, &Timestamp, &Sample);
		if (FAILED(Hr))
		{
			std::wcerr << L"ReadSample failed: 0x" << std::hex << Hr << L"\n";
			MFShutdown();
			return 1;
		}

		if (Flags & MF_SOURCE_READERF_ENDOFSTREAM)
		{
			break;
		}

		if (!Sample)
		{
			continue;
		}

		ComPtr<IMFMediaBuffer> Buffer;
		Hr = Sample->ConvertToContiguousBuffer(&Buffer);
		if (FAILED(Hr))
		{
			continue;
		}

		BYTE* Data = nullptr;
		DWORD MaxLength = 0;
		DWORD CurrentLength = 0;
		Hr = Buffer->Lock(&Data, &MaxLength, &CurrentLength);
		if (SUCCEEDED(Hr))
		{
			PcmData.insert(PcmData.end(), Data, Data + CurrentLength);
			Buffer->Unlock();
		}
	}

	if (PcmData.empty())
	{
		std::wcerr << L"No decoded PCM samples were produced\n";
		MFShutdown();
		return 1;
	}

	const bool bWrote = WriteWav(OutputPath, PcmData, SampleRate, Channels, BitsPerSample);
	MFShutdown();

	if (!bWrote)
	{
		return 1;
	}

	std::wcout << L"Decoded " << InputPath << L" -> " << OutputPath << L"\n";
	std::wcout << L"Format: " << SampleRate << L" Hz, " << Channels << L" channel(s), " << BitsPerSample << L" bit\n";
	return 0;
}
