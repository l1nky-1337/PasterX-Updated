#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <thread>
#include <iostream>
#include <functional>
#include <chrono>
#include <string>
#include "driverdefs.h"
#include <mutex>


std::mutex isuse;

class Driver
{
public:

	UINT ProcessId;

	const bool Init(const BOOL PhysicalMode) {
		this->bPhysicalMode = PhysicalMode;
		this->hDriver = CreateFileA((("\\\\.\\\SPACEPORT")), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
		if (this->hDriver != INVALID_HANDLE_VALUE) {
			if (this->SharedBuffer = VirtualAlloc(0, sizeof(REQUEST_DATA), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)) {
				UNICODE_STRING RegPath = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\ALG");
				if (!RegistryUtils::WriteRegistry(RegPath, RTL_CONSTANT_STRING(L"cac"), &this->SharedBuffer, REG_QWORD, 8)) {
					return false;
				}
				PVOID pid = (PVOID)GetCurrentProcessId();
				if (!RegistryUtils::WriteRegistry(RegPath, RTL_CONSTANT_STRING(L"pi"), &pid, REG_QWORD, 8)) {
					return false;
				}
				auto OLD_MAGGICCODE = this->MAGGICCODE;
				SendRequest(99, 0);
				if (this->MAGGICCODE == OLD_MAGGICCODE)
					this->MAGGICCODE = (ULONG64)RegistryUtils::ReadRegistry<LONG64>(RegPath, RTL_CONSTANT_STRING(L"awer"));
				return true;
			}
		}
		return false;
	}

	const NTSTATUS SendRequest(const UINT type, const PVOID args) {
		std::scoped_lock lock(isuse);
		REQUEST_DATA req;
		NTSTATUS status;
		req.MaggicCode = &this->MAGGICCODE;
		req.Type = type;
		req.Arguments = args;
		req.Status = &status;
		memcpy(this->SharedBuffer, &req, sizeof(REQUEST_DATA));
		FlushFileBuffers(this->hDriver);
		return status;
	}

	NTSTATUS ReadProcessMemory(uint64_t src, void* dest, uint32_t size) {
		REQUEST_READ req;
		req.ProcessId = ProcessId;
		req.Src = src;
		req.Dest = dest;
		req.Size = size;
		req.bPhysicalMem = bPhysicalMode;
		return SendRequest(REQUEST_TYPE::READ, &req);
	}
	NTSTATUS WriteProcessMemory(PVOID src, PVOID dest, DWORD size) {
		REQUEST_WRITE req;
		req.ProcessId = ProcessId;
		req.Src = src;
		req.Dest = dest;
		req.Size = size;
		req.bPhysicalMem = bPhysicalMode;
		return SendRequest(REQUEST_TYPE::WRITE, &req);
	}

	const UINT GetProcessThreadNumByID(DWORD dwPID)
	{
		HANDLE hProcessSnap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (hProcessSnap == INVALID_HANDLE_VALUE)
			return 0;

		PROCESSENTRY32 pe32 = { 0 };
		pe32.dwSize = sizeof(pe32);
		BOOL bRet = ::Process32First(hProcessSnap, &pe32);;
		while (bRet)
		{
			if (pe32.th32ProcessID == dwPID)
			{
				::CloseHandle(hProcessSnap);
				return pe32.cntThreads;
			}
			bRet = ::Process32Next(hProcessSnap, &pe32);
		}
		return 0;
	}
	NTSTATUS ZwQueryVirtualMemory(HANDLE ProcessHandle, PVOID BaseAddress, DWORD Type, PVOID Buffer, SIZE_T Size)
	{
		REQUEST_ZWQUERY req;
		req.ProcessHandle = ProcessHandle;
		req.BaseAddress = BaseAddress;
		req.Type = Type;
		req.Buffer = Buffer;
		req.Size = Size;
		return SendRequest(REQUEST_TYPE::ZWQUERY, &req);
	}
	NTSTATUS PsLookupProcessByProcessIdEx(HANDLE ProcessId, PVOID OutProcess)
	{
		REQUEST_PSLOOKUP req;
		req.ProcessId = ProcessId;
		req.OutProcess = OutProcess;
		return SendRequest(REQUEST_TYPE::PSLOOKUP, &req);
	}

	const bool Attach(DWORD ProcessID) {
		this->ProcessId = ProcessID;
		return true;
	}

	const uint64_t GetModuleBase(const wchar_t* ModuleName = 0) {
		if (bPhysicalMode) {
			REQUEST_MAINBASE req;
			uint64_t base = NULL;
			req.ProcessId = ProcessId;
			req.OutAddress = (PBYTE*)&base;
			SendRequest(REQUEST_TYPE::MAINBASE, &req);
			return { base };
		}
		else {
			if (!ModuleName)
				return { 0 };
			REQUEST_MODULE req;
			uint64_t base = NULL;
			DWORD size = NULL;
			req.ProcessId = ProcessId;
			req.OutAddress = (PBYTE*)&base;
			req.OutSize = &size;
			wcscpy_s(req.Module, sizeof(req.Module) / sizeof(req.Module[0]), ModuleName);
			SendRequest(REQUEST_TYPE::MODULE, &req);
			return { base };
		}
	}


private:
	PVOID SharedBuffer;
	HANDLE hDriver;
	ULONG64 MAGGICCODE = 0x25001730218c1e2dul;
	BOOL bPhysicalMode = FALSE;
	typedef enum _REQUEST_TYPE : UINT {
		WRITE,
		READ,
		PROTECT,
		ALLOC,
		FREE,
		MODULE,
		MAINBASE,
		THREADCALL,
		ZWQUERY,
		PSLOOKUP
	} REQUEST_TYPE;

	typedef struct _REQUEST_DATA {
		ULONG64* MaggicCode;
		UINT Type;
		PVOID Arguments;
		NTSTATUS* Status;
	} REQUEST_DATA, * PREQUEST_DATA;

	typedef struct _REQUEST_WRITE {
		DWORD ProcessId;
		PVOID Dest;
		PVOID Src;
		DWORD Size;
		BOOL bPhysicalMem;
	} REQUEST_WRITE, * PREQUEST_WRITE;

	typedef struct _REQUEST_READ {
		DWORD ProcessId;
		void* Dest;
		uint64_t Src;
		uint32_t Size;
		BOOL bPhysicalMem;
	} REQUEST_READ, * PREQUEST_READ;

	typedef struct _REQUEST_PROTECT {
		DWORD ProcessId;
		PVOID Address;
		DWORD Size;
		PDWORD InOutProtect;
	} REQUEST_PROTECT, * PREQUEST_PROTECT;

	typedef struct _REQUEST_ALLOC {
		DWORD ProcessId;
		PVOID OutAddress;
		DWORD Size;
		DWORD Protect;
	} REQUEST_ALLOC, * PREQUEST_ALLOC;

	typedef struct _REQUEST_FREE {
		DWORD ProcessId;
		PVOID Address;
	} REQUEST_FREE, * PREQUEST_FREE;

	typedef struct _REQUEST_MODULE {
		DWORD ProcessId;
		WCHAR Module[0xFF];
		PBYTE* OutAddress;
		DWORD* OutSize;
	} REQUEST_MODULE, * PREQUEST_MODULE;

	typedef struct _REQUEST_MAINBASE {
		DWORD ProcessId;
		PBYTE* OutAddress;
	} REQUEST_MAINBASE, * PREQUEST_MAINBASE;

	typedef struct _REQUEST_ZWQUERY {
		HANDLE ProcessHandle;
		PVOID BaseAddress;
		DWORD Type;
		PVOID Buffer;
		SIZE_T Size;
	} REQUEST_ZWQUERY, * PREQUEST_ZWQUERY;

	typedef struct _REQUEST_PSLOOKUP {
		HANDLE ProcessId;
		PVOID OutProcess;
	} REQUEST_PSLOOKUP, * PREQUEST_PSLOOKUP;

};

Driver* driver = new Driver;

template <typename T>
T read(const uintptr_t address)
{
	T buffer;
	driver->ReadProcessMemory(address, &buffer, sizeof(T));
	return buffer;
}
template <typename T>
T write(const uintptr_t address, T buffer)
{
	driver->WriteProcessMemory((PVOID)&buffer, (PVOID)address, sizeof(T));
	return buffer;
}