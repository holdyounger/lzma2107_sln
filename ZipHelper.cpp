#include <stdio.h>
#include "ZipHelper.h"

#include "./lzma2107/CPP/Common/Common.h"
#include "./lzma2107/CPP/Common/MyWindows.h"
#include "./lzma2107/CPP/Common/Defs.h"
#include "./lzma2107/CPP/Common/MyInitGuid.h"
#include "./lzma2107/CPP/Common/IntToString.h"
#include "./lzma2107/CPP/Common/StringConvert.h"

#include "./lzma2107/CPP/Windows/DLL.h"
#include "./lzma2107/CPP/Windows/FileDir.h"
#include "./lzma2107/CPP/Windows/FileFind.h"
#include "./lzma2107/CPP/Windows/FileName.h"
#include "./lzma2107/CPP/Windows/NtCheck.h"
#include "./lzma2107/CPP/Windows/PropVariant.h"
#include "./lzma2107/CPP/Windows/PropVariantConv.h"

#include "./lzma2107/CPP/7zip/Common/FileStreams.h"
#include "./lzma2107/CPP/7zip/Archive/IArchive.h"
#include "./lzma2107/CPP/7zip/IPassword.h"

#include "./lzma2107/C/7zVersion.h"

DEFINE_GUID(CLSID_CFormatZip,
    0x23170F69, 0x40C1, 0x278A, 0x10, 0x00, 0x00, 0x01, 0x10, 0x01, 0x00, 0x00);

#define CLSID_Format CLSID_CFormatZip

using namespace NWindows;
using namespace NFile;
using namespace NDir;

#include <vector>
using namespace std;

struct CDirItem
{
    UInt64 Size;
    FILETIME CTime;
    FILETIME ATime;
    FILETIME MTime;
    UString Name;
    FString FullPath;
    UInt32 Attrib;

    bool isDir() const { return (Attrib & FILE_ATTRIBUTE_DIRECTORY) != 0; }
};

class CArchiveUpdateCallback :
    public IArchiveUpdateCallback2,
    public ICryptoGetTextPassword2,
    public CMyUnknownImp
{
public:
    MY_UNKNOWN_IMP2(IArchiveUpdateCallback2, ICryptoGetTextPassword2)

        // IProgress
        STDMETHOD(SetTotal)(UInt64 size);
    STDMETHOD(SetCompleted)(const UInt64* completeValue);

    // IUpdateCallback2
    STDMETHOD(GetUpdateItemInfo)(UInt32 index,
        Int32* newData, Int32* newProperties, UInt32* indexInArchive);
    STDMETHOD(GetProperty)(UInt32 index, PROPID propID, PROPVARIANT* value);
    STDMETHOD(GetStream)(UInt32 index, ISequentialInStream** inStream);
    STDMETHOD(SetOperationResult)(Int32 operationResult);
    STDMETHOD(GetVolumeSize)(UInt32 index, UInt64* size);
    STDMETHOD(GetVolumeStream)(UInt32 index, ISequentialOutStream** volumeStream);

    STDMETHOD(CryptoGetTextPassword2)(Int32* passwordIsDefined, BSTR* password);

public:
    CRecordVector<UInt64> VolumesSizes;
    UString VolName;
    UString VolExt;

    FString DirPrefix;
    const CObjectVector<CDirItem>* DirItems;

    bool PasswordIsDefined;
    UString Password;
    bool AskPassword;

    bool m_NeedBeClosed;

    FStringVector FailedFiles;
    CRecordVector<HRESULT> FailedCodes;

    CArchiveUpdateCallback() :
        DirItems(NULL),
        PasswordIsDefined(false),
        AskPassword(false)
    {}

    ~CArchiveUpdateCallback() { Finilize(); }
    HRESULT Finilize();

    void Init(const CObjectVector<CDirItem>* dirItems)
    {
        DirItems = dirItems;
        m_NeedBeClosed = false;
        FailedFiles.Clear();
        FailedCodes.Clear();
    }
};

STDAPI CreateObject(const GUID* clsid, const GUID* iid, void** outObject);

int QZipComprocess(CString strArchive, CString strPassword, vector<CString>& vetFileList)
{
    int iRet = 0;

    CObjectVector<CDirItem> dirItems;
    for (auto& file : vetFileList)
    {
        CString strFile = file;
        CDirItem di;
        FString name = strFile;
        NFind::CFileInfo fi;
        if (!fi.Find(name))
        {
            printf("Can't find file[%s]", strFile);
            //return 1;
            continue;
        }

        di.Attrib = fi.Attrib;
        di.Size = fi.Size;
        di.CTime = fi.CTime;
        di.ATime = fi.ATime;
        di.MTime = fi.MTime;
        //di.Name = fs2us(name);
        di.Name = fi.Name; /*fs2us(name)*/;
        di.FullPath = name;
        dirItems.Add(di);
    }

    COutFileStream* outFileStreamSpec = new COutFileStream;
    CMyComPtr<IOutStream> outFileStream = outFileStreamSpec;
    if (!outFileStreamSpec->Create(strArchive, true))
    {
        //PrintError("can't create archive file");
        printf("[QZipComprocess]无法创建压缩文件[%s],GetLastError=%d", strArchive, GetLastError());
        return 1;
    }

    CMyComPtr<IOutArchive> outArchive;
    if (CreateObject(&CLSID_Format, &IID_IOutArchive, (void**)&outArchive) != S_OK)
    {
        //PrintError("Can not get class object");
        printf("[QZipComprocess]CreateObject[CLSID_Format]失败[%s],GetLastError=%d", strArchive, GetLastError());
        return 2;
    }

    CArchiveUpdateCallback* updateCallbackSpec = new CArchiveUpdateCallback;
#if 1
    updateCallbackSpec->PasswordIsDefined = true;
    // updateCallbackSpec->AskPassword = false;
    updateCallbackSpec->Password = strPassword;
#endif

    CMyComPtr<IArchiveUpdateCallback2> updateCallback(updateCallbackSpec);
    updateCallbackSpec->Init(&dirItems);

    HRESULT result = outArchive->UpdateItems(outFileStream, dirItems.Size(), updateCallback);

    updateCallbackSpec->Finilize();

    if (result != S_OK)
    {
        printf("[QZipComprocess]Update Error(%S),UpdateItems return %d,GetLastError=%d", strArchive.GetBuffer(), result, GetLastError());
        return 3;
    }

    FOR_VECTOR(i, updateCallbackSpec->FailedFiles)
    {
        //PrintNewLine();
        //PrintError("Error for file", updateCallbackSpec->FailedFiles[i]);
    }

    if (updateCallbackSpec->FailedFiles.Size() != 0) {
        printf("[[QZipComprocess]updateCallbackSpec->FailedFiles.Size() != 0[%s],GetLastError=%d", strArchive, GetLastError());
        return 4;
    }

    return iRet;
}
