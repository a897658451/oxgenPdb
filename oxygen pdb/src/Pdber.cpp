#include "Pdber.hpp"
#include <log.h>



namespace oxygenPdb {
	extern "C" wchar_t* __cdecl wcscat(wchar_t* Dest, const wchar_t* Source);
	extern "C" wchar_t* __cdecl wcscpy(wchar_t* Dest, const wchar_t* Source);
	extern "C" char* __cdecl strcpy(char* Dest, const char* Source);

	NTSTATUS CreateDirectory(PUNICODE_STRING DirectoryName) {
		OBJECT_ATTRIBUTES objAttrs;
		HANDLE handle;
		IO_STATUS_BLOCK ioStatus;
		NTSTATUS status;

		InitializeObjectAttributes(&objAttrs, DirectoryName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

		status = ZwCreateFile(
			&handle,
			FILE_LIST_DIRECTORY | SYNCHRONIZE,
			&objAttrs,
			&ioStatus,
			NULL,
			FILE_ATTRIBUTE_DIRECTORY,
			0,
			FILE_CREATE,
			FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,
			NULL,
			0
		);

		if (NT_SUCCESS(status)) {
			ZwClose(handle);
		}

		return status;
	}

	NTSTATUS CheckAndCreateDirectory(PCWSTR DirectoryPath) {
		UNICODE_STRING uniName;
		OBJECT_ATTRIBUTES objAttrs;
		HANDLE handle;
		IO_STATUS_BLOCK ioStatus;
		NTSTATUS status;
		FILE_BASIC_INFORMATION fileInfo;

		RtlInitUnicodeString(&uniName, DirectoryPath);
		InitializeObjectAttributes(&objAttrs, &uniName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

		status = ZwOpenFile(
			&handle,
			FILE_LIST_DIRECTORY | SYNCHRONIZE,
			&objAttrs,
			&ioStatus,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT
		);

		if (NT_SUCCESS(status)) {
			status = ZwQueryInformationFile(handle, &ioStatus, &fileInfo, sizeof(fileInfo), FileBasicInformation);
			ZwClose(handle);

			if (NT_SUCCESS(status)) {
				if (fileInfo.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
					return STATUS_SUCCESS;  // 目录存在
				}
				else {
					return STATUS_NOT_A_DIRECTORY;  // 存在但不是目录
				}
			}
		}

		// 目录不存在，尝试创建
		status = CreateDirectory(&uniName);
		if (NT_SUCCESS(status)) {
			return STATUS_SUCCESS;
		}
		else {
			return status;  // 创建目录失败
		}
	}

	Pdber::Pdber(const wchar_t* moduleName) :_moduler(moduleName), _initfailed(false)
	{
		CheckAndCreateDirectory(L"\\??\\C:\\Windows\\PDB");
	}

	bool Pdber::init()
	{
		const auto [base, size] = _moduler.getModuleInfo();
		do {
			if (base == 0 || size == 0) {
				break;
			}
			//viewer class to get pdb guid(md5 of pdb file and pdb name) 
			//move semantics will done when as a func return value
			auto info = _pdbViewer.getPdbInfo(base);


			//then download pdb or open pdb(pdb has exits
			const auto cpath = _pdbViewer.downLoadPdb(info).data();

			if (cpath == kstd::wstring(L"")) break;
			wcscpy(_pdbPath, cpath);
			strcpy(_pdbGuid, info.first.data());

			//init pdber file stream
			wchar_t wPdbFilePath[MAX_PATH]{ 0 };
			wcscpy(wPdbFilePath, L"\\??\\");
			wcscat(wPdbFilePath, _pdbPath);
			symbolic_access::FileStream pdbFileStream(wPdbFilePath);
			symbolic_access::MsfReader msfReader(std::move(pdbFileStream));
			if (!msfReader.Initialize())
			{
				printk("Failed to initialize msf reader %\r\n");
				break;
			}

			//symbol map 就是流->符号的映射
			symbolic_access::SymbolsExtractor symbolsExtractor(msfReader);
			_symbols = symbolsExtractor.Extract();
			if (_symbols.empty())
			{
				printk("Failed to extract symbols for %\r\n");
				break;
			}

			_structs = symbolic_access::StructExtractor(msfReader).Extract();
			return true;

		} while (0);

		//happens an error when executing there!
		printk("failed to open or download pdb file!\r\n");
		_initfailed = true;
		return false;
	}

	Pdber::~Pdber()
	{
		if (_initfailed) {
			return;
		}
		else {


			return;
		}


	}




}