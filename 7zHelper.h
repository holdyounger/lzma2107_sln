#pragma once
#include <atlstr.h>
#include <vector>
using namespace std;

//Usage:  Demo����
//vector<CString>   vecFileList;
//CString   strArchive=L"d:\\test\\a.7z";
//CString  strFile1 = L"d:\\a.txt";
//CString  strFile2 = L"d:\\b.txt";
//vecFileList.push_back(strFile1);
//vecFileList.push_back(strFile2);
//Q7zComprocess(strArchive,vecFileList);
//vecFileList.clear();

//����ֵ0 ��ʾ�ɹ�
//strArchive 7z������·��
//vetFileList������ѹ���ļ��ľ���·��
//Ŀǰֻ֧��ѹ��������Ŀ¼
int Q7zComprocess(CString  strArchive, CString strPassword, vector<CString> &vetFileList);