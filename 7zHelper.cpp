

//////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include "7zHelper.h"

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


DEFINE_GUID(CLSID_CFormat7z,
            0x23170F69, 0x40C1, 0x278A, 0x10, 0x00, 0x00, 0x01, 0x10, 0x07, 0x00, 0x00);
DEFINE_GUID(CLSID_CFormatXz,
            0x23170F69, 0x40C1, 0x278A, 0x10, 0x00, 0x00, 0x01, 0x10, 0x0C, 0x00, 0x00);

#define CLSID_Format CLSID_CFormat7z
// #define CLSID_Format CLSID_CFormatXz

using namespace NWindows;
using namespace NFile;
using namespace NDir;

#include <vector>
using namespace std;





static void Convert_UString_to_AString(const UString &s, AString &temp)
{
  int codePage = CP_OEMCP;
  /*
  int g_CodePage = -1;
  int codePage = g_CodePage;
  if (codePage == -1)
    codePage = CP_OEMCP;
  if (codePage == CP_UTF8)
    ConvertUnicodeToUTF8(s, temp);
  else
  */
    UnicodeStringToMultiByte2(temp, s, (UINT)codePage);
}

static FString CmdStringToFString(const char *s)
{
  return us2fs(GetUnicodeString(s));
}

static void Print(const char *s)
{
  fputs(s, stdout);
}

static void Print(const AString &s)
{
  //Print(s.Ptr());
}

static void Print(const UString &s)
{
  AString as;
  Convert_UString_to_AString(s, as);
  Print(as);
}

static void Print(const wchar_t *s)
{
  Print(UString(s));
}

static void PrintNewLine()
{
  //Print("\n");
}

static void PrintStringLn(const char *s)
{
  Print(s);
  PrintNewLine();
}

static void PrintError(const char *message)
{
  Print("Error: ");
  PrintNewLine();
  Print(message);
  PrintNewLine();
}

static void PrintError(const char *message, const FString &name)
{
  PrintError(message);
  Print(name);
}

static HRESULT IsArchiveItemProp(IInArchive *archive, UInt32 index, PROPID propID, bool &result)
{
    NCOM::CPropVariant prop;
    RINOK(archive->GetProperty(index, propID, &prop));
    if (prop.vt == VT_BOOL)
        result = VARIANT_BOOLToBool(prop.boolVal);
    else if (prop.vt == VT_EMPTY)
        result = false;
    else
        return E_FAIL;
    return S_OK;
}

static HRESULT IsArchiveItemFolder(IInArchive *archive, UInt32 index, bool &result)
{
    return IsArchiveItemProp(archive, index, kpidIsDir, result);
}


static const wchar_t * const kEmptyFileAlias = L"[Content]";


////////////////////////////////////////////////////////////////
// Archive Creating callback class

struct CDirItem
{
    UInt64 Size;
    FILETIME CTime;
    FILETIME ATime;
    FILETIME MTime;
    UString Name;
    FString FullPath;
    UInt32 Attrib;

    bool isDir() const { return (Attrib & FILE_ATTRIBUTE_DIRECTORY) != 0 ; }
};

class CArchiveUpdateCallback:
    public IArchiveUpdateCallback2,
    public ICryptoGetTextPassword2,
    public CMyUnknownImp
{
public:
    MY_UNKNOWN_IMP2(IArchiveUpdateCallback2, ICryptoGetTextPassword2)

        // IProgress
        STDMETHOD(SetTotal)(UInt64 size);
    STDMETHOD(SetCompleted)(const UInt64 *completeValue);

    // IUpdateCallback2
    STDMETHOD(GetUpdateItemInfo)(UInt32 index,
        Int32 *newData, Int32 *newProperties, UInt32 *indexInArchive);
    STDMETHOD(GetProperty)(UInt32 index, PROPID propID, PROPVARIANT *value);
    STDMETHOD(GetStream)(UInt32 index, ISequentialInStream **inStream);
    STDMETHOD(SetOperationResult)(Int32 operationResult);
    STDMETHOD(GetVolumeSize)(UInt32 index, UInt64 *size);
    STDMETHOD(GetVolumeStream)(UInt32 index, ISequentialOutStream **volumeStream);

    STDMETHOD(CryptoGetTextPassword2)(Int32 *passwordIsDefined, BSTR *password);

public:
    CRecordVector<UInt64> VolumesSizes;
    UString VolName;
    UString VolExt;

    FString DirPrefix;
    const CObjectVector<CDirItem> *DirItems;

    bool PasswordIsDefined;
    UString Password;
    bool AskPassword;

    bool m_NeedBeClosed;

    FStringVector FailedFiles;
    CRecordVector<HRESULT> FailedCodes;

    CArchiveUpdateCallback():
    DirItems(NULL),
        PasswordIsDefined(false),
        AskPassword(false)
    {}

    ~CArchiveUpdateCallback() { Finilize(); }
    HRESULT Finilize();

    void Init(const CObjectVector<CDirItem> *dirItems)
    {
        DirItems = dirItems;
        m_NeedBeClosed = false;
        FailedFiles.Clear();
        FailedCodes.Clear();
    }
};

STDMETHODIMP CArchiveUpdateCallback::SetTotal(UInt64 /* size */)
{
    return S_OK;
}

STDMETHODIMP CArchiveUpdateCallback::SetCompleted(const UInt64 * /* completeValue */)
{
    return S_OK;
}

STDMETHODIMP CArchiveUpdateCallback::GetUpdateItemInfo(UInt32 /* index */,
                                                       Int32 *newData, Int32 *newProperties, UInt32 *indexInArchive)
{
    if (newData)
        *newData = BoolToInt(true);
    if (newProperties)
        *newProperties = BoolToInt(true);
    if (indexInArchive)
        *indexInArchive = (UInt32)(Int32)-1;
    return S_OK;
}

STDMETHODIMP CArchiveUpdateCallback::GetProperty(UInt32 index, PROPID propID, PROPVARIANT *value)
{
    NCOM::CPropVariant prop;

    if (propID == kpidIsAnti)
    {
        prop = false;
        prop.Detach(value);
        return S_OK;
    }

    {
        const CDirItem &dirItem = (*DirItems)[index];
        switch (propID)
        {
        case kpidPath:  prop = dirItem.Name; break;
        case kpidIsDir:  prop = dirItem.isDir(); break;
        case kpidSize:  prop = dirItem.Size; break;
        case kpidAttrib:  prop = dirItem.Attrib; break;
        case kpidCTime:  prop = dirItem.CTime; break;
        case kpidATime:  prop = dirItem.ATime; break;
        case kpidMTime:  prop = dirItem.MTime; break;
        }
    }
    prop.Detach(value);
    return S_OK;
}

HRESULT CArchiveUpdateCallback::Finilize()
{
    if (m_NeedBeClosed)
    {
        PrintNewLine();
        m_NeedBeClosed = false;
    }
    return S_OK;
}

static void GetStream2(const wchar_t *name)
{
    //Print("Compressing  ");
    if (name[0] == 0)
        name = kEmptyFileAlias;
    //Print(name);
}

STDMETHODIMP CArchiveUpdateCallback::GetStream(UInt32 index, ISequentialInStream **inStream)
{
    RINOK(Finilize());

    const CDirItem &dirItem = (*DirItems)[index];
    GetStream2(dirItem.Name);

    if (dirItem.isDir())
        return S_OK;

    {
        CInFileStream *inStreamSpec = new CInFileStream;
        CMyComPtr<ISequentialInStream> inStreamLoc(inStreamSpec);
        FString path = DirPrefix + dirItem.FullPath;
        if (!inStreamSpec->Open(path))
        {
            DWORD sysError = ::GetLastError();
            FailedCodes.Add(sysError);
            FailedFiles.Add(path);
            // if (systemError == ERROR_SHARING_VIOLATION)
            {
                PrintNewLine();
                PrintError("WARNING: can't open file");
                // Print(NError::MyFormatMessageW(systemError));
                return S_FALSE;
            }
            // return sysError;
        }
        *inStream = inStreamLoc.Detach();
    }
    return S_OK;
}

STDMETHODIMP CArchiveUpdateCallback::SetOperationResult(Int32 /* operationResult */)
{
    m_NeedBeClosed = true;
    return S_OK;
}

STDMETHODIMP CArchiveUpdateCallback::GetVolumeSize(UInt32 index, UInt64 *size)
{
    if (VolumesSizes.Size() == 0)
        return S_FALSE;
    if (index >= (UInt32)VolumesSizes.Size())
        index = VolumesSizes.Size() - 1;
    *size = VolumesSizes[index];
    return S_OK;
}

STDMETHODIMP CArchiveUpdateCallback::GetVolumeStream(UInt32 index, ISequentialOutStream **volumeStream)
{
    wchar_t temp[16];
    ConvertUInt32ToString(index + 1, temp);
    UString res = temp;
    while (res.Len() < 2)
        res.InsertAtFront(L'0');
    UString fileName = VolName;
    fileName += '.';
    fileName += res;
    fileName += VolExt;
    COutFileStream *streamSpec = new COutFileStream;
    CMyComPtr<ISequentialOutStream> streamLoc(streamSpec);
    if (!streamSpec->Create(us2fs(fileName), false))
        return ::GetLastError();
    *volumeStream = streamLoc.Detach();
    return S_OK;
}

STDMETHODIMP CArchiveUpdateCallback::CryptoGetTextPassword2(Int32 *passwordIsDefined, BSTR *password)
{
    if (!PasswordIsDefined)
    {
        if (AskPassword)
        {
            // You can ask real password here from user
            // Password = GetPassword(OutStream);
            // PasswordIsDefined = true;
            PrintError("Password is not defined");
            return E_ABORT;
        }
    }
    *passwordIsDefined = BoolToInt(PasswordIsDefined);
    return StringToBstr(Password, password);
}



STDAPI CreateObject(const GUID *clsid, const GUID *iid, void **outObject);

int Q7zComprocess(CString strArchive, CString strPassword, vector<CString> &vetFileList)
{
    int iRet=0;

    CObjectVector<CDirItem> dirItems;
    for (auto &file : vetFileList)
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

    COutFileStream *outFileStreamSpec = new COutFileStream;
    CMyComPtr<IOutStream> outFileStream = outFileStreamSpec;
    if (!outFileStreamSpec->Create(strArchive, true))
    {
        //PrintError("can't create archive file");
		printf("[Q7zComprocess]无法创建压缩文件[%s],GetLastError=%d",strArchive,GetLastError());
        return 1;
    }

    CMyComPtr<IOutArchive> outArchive;
    if (CreateObject(&CLSID_Format, &IID_IOutArchive, (void **)&outArchive) != S_OK)
    {
        //PrintError("Can not get class object");
		printf("[Q7zComprocess]CreateObject[CLSID_Format]失败[%s],GetLastError=%d",strArchive,GetLastError());
        return 2;
    }

    CArchiveUpdateCallback *updateCallbackSpec = new CArchiveUpdateCallback;
#if 1
    updateCallbackSpec->PasswordIsDefined = true;
    // updateCallbackSpec->AskPassword = false;
    updateCallbackSpec->Password = strPassword;
#endif

    CMyComPtr<IArchiveUpdateCallback2> updateCallback(updateCallbackSpec);
    updateCallbackSpec->Init(&dirItems);

    HRESULT result = outArchive->UpdateItems(outFileStream, dirItems.Size(), updateCallback);

    updateCallbackSpec->Finilize();
    E_FAIL;
    if (result != S_OK)
    {
		printf("[Q7zComprocess]Update Error(%S),UpdateItems return %d,GetLastError=%d",strArchive.GetBuffer(), result, GetLastError());
        return 3;
    }

    FOR_VECTOR (i, updateCallbackSpec->FailedFiles)
    {
        //PrintNewLine();
        //PrintError("Error for file", updateCallbackSpec->FailedFiles[i]);
    }

    if (updateCallbackSpec->FailedFiles.Size() != 0){
		printf("[[Q7zComprocess]updateCallbackSpec->FailedFiles.Size() != 0[%s],GetLastError=%d",strArchive,GetLastError());
        return 4;
    }

    return iRet;
}
