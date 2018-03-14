//  Differ.cpp
//  ZipDiff
/*
 The MIT License (MIT)
 Copyright (c) 2018 HouSisong
 
 Permission is hereby granted, free of charge, to any person
 obtaining a copy of this software and associated documentation
 files (the "Software"), to deal in the Software without
 restriction, including without limitation the rights to use,
 copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the
 Software is furnished to do so, subject to the following
 conditions:
 
 The above copyright notice and this permission notice shall be
 included in all copies of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 OTHER DEALINGS IN THE SOFTWARE.
 */
#include "Differ.h"
#include <iostream>
#include <stdio.h>
#include "../../HDiffPatch/libHDiffPatch/HDiff/diff.h"  //https://github.com/sisong/HDiffPatch
#include "../../HDiffPatch/libHDiffPatch/HPatch/patch.h"
#include "../../HDiffPatch/file_for_patch.h"
#include "../../HDiffPatch/_clock_for_demo.h"
#include "../patch/patch_types.h"
#include "../../HDiffPatch/compress_plugin_demo.h"
#include "../../HDiffPatch/decompress_plugin_demo.h"
#include "../patch/OldStream.h"
#include "../patch/Patcher.h"
#include "OldRef.h"
#include "DiffData.h"

#ifdef _CompressPlugin_zstd
const hdiff_TStreamCompress* __not_used_for_compiler__zstd =&zstdStreamCompressPlugin;
#endif
const hdiff_TStreamCompress* __not_used_for_compiler__zlib =&zlibStreamCompressPlugin;

bool checkZipInfo(UnZipper* oldZip,UnZipper* newZip);
bool HDiffZ(const std::vector<TByte>& oldData,const std::vector<TByte>& newData,std::vector<TByte>& out_diffData,
            hdiff_TCompress* compressPlugin,hpatch_TDecompress* decompressPlugin,int myBestMatchScore);
bool testZipPatch(const char* oldZipPath,const char* zipDiffPath,const char* outNewZipPath);
bool checkZipIsSame(const char* oldZipPath,const char* newZipPath,bool byteByByteCheckSame);

#define  check(value) { \
    if (!(value)){ printf(#value" ERROR!\n");  \
        result=false; if (!_isInClear){ goto clear; } } }


bool ZipDiff(const char* oldZipPath,const char* newZipPath,const char* outDiffFileName,const char* temp_ZipPatchFileName){
    const int           myBestMatchScore=3;
    UnZipper            oldZip;
    UnZipper            newZip;
    TFileStreamOutput   out_diffFile;
    std::vector<TByte>  newData;
    std::vector<TByte>  oldData;
    std::vector<TByte>  hdiffzData;
    std::vector<TByte>  out_diffData;
    std::vector<uint32_t> samePairList;
    std::vector<uint32_t> newRefList;
    std::vector<uint32_t> newRefNotDecompressList;
    std::vector<uint32_t> newRefCompressedSizeList;
    std::vector<uint32_t> oldRefList;
    std::vector<uint32_t> oldRefNotDecompressList;
    bool            result=true;
    bool            _isInClear=false;
    bool            byteByByteCheckSame=false;
    size_t          newZipAlignSize=0;
    int             oldZipNormalized_compressLevel=kDefaultZlibCompressLevel;
    int             newZipNormalized_compressLevel=kDefaultZlibCompressLevel;
#ifdef _CompressPlugin_zstd
    zstd_compress_level=22; //0..22
    hdiff_TCompress* compressPlugin=&zstdCompressPlugin;
    hpatch_TDecompress* decompressPlugin=&zstdDecompressPlugin;
#else
    zlib_compress_level=9; //0..9
    hdiff_TCompress* compressPlugin=&zlibCompressPlugin;
    hpatch_TDecompress* decompressPlugin=&zlibDecompressPlugin;
#endif
    UnZipper_init(&oldZip);
    UnZipper_init(&newZip);
    TFileStreamOutput_init(&out_diffFile);
    
    check(UnZipper_openRead(&oldZip,oldZipPath));
    check(UnZipper_openRead(&newZip,newZipPath));
    newZip._isDataNormalized=getZipCompressedDataIsNormalized(&newZip,&newZipNormalized_compressLevel);
    oldZip._isDataNormalized=getZipCompressedDataIsNormalized(&oldZip,&oldZipNormalized_compressLevel);
    if (UnZipper_isHaveApkV2Sign(&newZip))
        oldZip._isDataNormalized&=(oldZipNormalized_compressLevel==newZipNormalized_compressLevel);
    newZipAlignSize=getZipAlignSize_unsafe(&newZip);
    if (UnZipper_isHaveApkV2Sign(&newZip))
        newZip._isDataNormalized&=(newZipAlignSize>0);//precondition (+checkZipIsSame() to complete)
    newZipAlignSize=(newZipAlignSize>0)?newZipAlignSize:kDefaultZipAlignSize;
    byteByByteCheckSame=UnZipper_isHaveApkV2Sign(&newZip);
    check(checkZipInfo(&oldZip,&newZip));
    
    std::cout<<"ZipDiff with compress plugin: \""<<compressPlugin->compressType(compressPlugin)<<"\"\n";
    check(getSamePairList(&newZip,&oldZip,newZipNormalized_compressLevel,samePairList,newRefList,
                          newRefNotDecompressList,newRefCompressedSizeList));
    check(getOldRefList(&newZip,samePairList,newRefList,newRefNotDecompressList,
                        &oldZip,oldRefList,oldRefNotDecompressList));
    std::cout<<"ZipDiff same file count: "<<samePairList.size()/2<<"\n";
    std::cout<<"    diff new file count: "<<newRefList.size()+newRefNotDecompressList.size()<<"\n";
    std::cout<<"     ref old file count: "<<oldRefList.size()+oldRefNotDecompressList.size()<<" ("
        <<UnZipper_fileCount(&oldZip)<<")\n";
    std::cout<<"     ref old decompress: "
        <<OldStream_getDecompressFileCount(&oldZip,oldRefList.data(),oldRefList.size())<<" file ("
        <<OldStream_getDecompressSumSize(&oldZip,oldRefList.data(),oldRefList.size()) <<" byte!)\n";
    //for (int i=0; i<(int)newRefList.size(); ++i) std::cout<<zipFile_name(&newZip,newRefList[i])<<"\n";
    //for (int i=0; i<(int)newRefNotDecompressList.size(); ++i) std::cout<<zipFile_name(&newZip,newRefNotDecompressList[i])<<"\n";
    //for (int i=0; i<(int)oldRefList.size(); ++i) std::cout<<zipFile_name(&oldZip,oldRefList[i])<<"\n";
    //for (int i=0; i<(int)oldRefNotDecompressList.size(); ++i) std::cout<<zipFile_name(&oldZip,oldRefNotDecompressList[i])<<"\n";

    check(readZipStreamData(&newZip,newRefList,newRefNotDecompressList,newData));
    check(readZipStreamData(&oldZip,oldRefList,oldRefNotDecompressList,oldData));
    check(HDiffZ(oldData,newData,hdiffzData,compressPlugin,decompressPlugin,myBestMatchScore));
    { std::vector<TByte> _empty; oldData.swap(_empty); }
    { std::vector<TByte> _empty; newData.swap(_empty); }
    
    check(serializeZipDiffData(out_diffData,&newZip,&oldZip,newZipAlignSize,newZipNormalized_compressLevel,
                               samePairList,newRefNotDecompressList,newRefCompressedSizeList,
                               oldRefList,oldRefNotDecompressList,hdiffzData,compressPlugin));
    std::cout<<"\nZipDiff size: "<<out_diffData.size()<<"\n";

    check(TFileStreamOutput_open(&out_diffFile,outDiffFileName,out_diffData.size()));
    check((long)out_diffData.size()==out_diffFile.base.write(out_diffFile.base.streamHandle,
                                        0,out_diffData.data(),out_diffData.data()+out_diffData.size()));
    check(TFileStreamOutput_close(&out_diffFile));
    std::cout<<"  out ZipDiff file ok!\n";
    check(UnZipper_close(&newZip));
    check(UnZipper_close(&oldZip));
    
    check(testZipPatch(oldZipPath,outDiffFileName,temp_ZipPatchFileName));
    check(checkZipIsSame(newZipPath,temp_ZipPatchFileName,byteByByteCheckSame));
    
clear:
    _isInClear=true;
    check(TFileStreamOutput_close(&out_diffFile));
    check(UnZipper_close(&newZip));
    check(UnZipper_close(&oldZip));
    return result;
}

bool checkZipInfo(UnZipper* oldZip,UnZipper* newZip){
    if (oldZip->_isDataNormalized)
        printf("  NOTE: oldZip Normalized\n");
    if (UnZipper_isHaveApkV1_or_jarSign(oldZip))
        printf("  NOTE: oldZip found JarSign(ApkV1Sign)\n");
    if (UnZipper_isHaveApkV2Sign(oldZip))
        printf("  NOTE: oldZip found ApkV2Sign\n");
    if (newZip->_isDataNormalized)
        printf("  NOTE: newZip Normalized\n");
    if (UnZipper_isHaveApkV1_or_jarSign(newZip))
        printf("  NOTE: newZip found JarSign(ApkV1Sign)\n");
    bool newIsV2Sign=UnZipper_isHaveApkV2Sign(newZip);
    if (newIsV2Sign)
        printf("  NOTE: newZip found ApkV2Sign\n");
    
    if (newIsV2Sign&(!newZip->_isDataNormalized)){
        printf("  ERROR: newZip not Normalized, need run ApkNormalized(newZip) before run ZipDiff!\n");
        return false;
    }
    return true;
}

bool HDiffZ(const std::vector<TByte>& oldData,const std::vector<TByte>& newData,std::vector<TByte>& out_diffData,
            hdiff_TCompress* compressPlugin,hpatch_TDecompress* decompressPlugin,int myBestMatchScore){
    double time0=clock_s();
    const size_t oldDataSize=oldData.size();
    const size_t newDataSize=newData.size();
    std::cout<<"\nrun HDiffZ:\n";
    std::cout<<"  oldDataSize : "<<oldDataSize<<"\n  newDataSize : "<<newDataSize<<"\n";
    
    std::vector<TByte>& diffData=out_diffData;
    const TByte* newData0=newData.data();
    const TByte* oldData0=oldData.data();
    create_compressed_diff(newData0,newData0+newDataSize,oldData0,oldData0+oldDataSize,
                           diffData,compressPlugin,myBestMatchScore);
    double time1=clock_s();
    std::cout<<"  diffDataSize: "<<diffData.size()<<"\n";
    std::cout<<"  diff  time: "<<(time1-time0)<<" s\n";
    if (!check_compressed_diff(newData0,newData0+newDataSize,oldData0,oldData0+oldDataSize,
                               diffData.data(),diffData.data()+diffData.size(),decompressPlugin)){
        std::cout<<"\n  HPatch check HDiffZ result error!!!\n";
        return false;
    }else{
        double time2=clock_s();
        std::cout<<"  HPatch check HDiffZ result ok!\n";
        std::cout<<"  hpatch time: "<<(time2-time1)<<" s\n";
        return true;
    }
}


bool testZipPatch(const char* oldZipPath,const char* zipDiffPath,const char* outNewZipPath){
    double time0=clock_s();
    TPatchResult ret=ZipPatch(oldZipPath,zipDiffPath,outNewZipPath,0,0);
    double time1=clock_s();
    if (ret==PATCH_SUCCESS){
        std::cout<<"\nrun ZipPatch ok!\n";
        std::cout<<"  patch time: "<<(time1-time0)<<" s\n";
        return true;
    }else{
        return false;
    }
}


static bool getFileIsSame(const char* xFileName,const char* yFileName){
    TFileStreamInput x;
    TFileStreamInput y;
    bool            result=true;
    bool            _isInClear=false;
    std::vector<TByte> buf;
    size_t          fileSize;
    TFileStreamInput_init(&x);
    TFileStreamInput_init(&y);
    check(TFileStreamInput_open(&x,xFileName));
    check(TFileStreamInput_open(&y,yFileName));
    fileSize=(size_t)x.base.streamSize;
    assert(fileSize==x.base.streamSize);
    check(fileSize==y.base.streamSize);
    if (fileSize>0){
        buf.resize(fileSize*2);
        check((long)fileSize==x.base.read(x.base.streamHandle,0,buf.data(),buf.data()+fileSize));
        check((long)fileSize==y.base.read(y.base.streamHandle,0,buf.data()+fileSize,buf.data()+fileSize*2));
        check(0==memcmp(buf.data(),buf.data()+fileSize,fileSize));
    }
clear:
    _isInClear=true;
    TFileStreamInput_close(&x);
    TFileStreamInput_close(&y);
    return result;
}

bool checkZipIsSame(const char* oldZipPath,const char* newZipPath,bool byteByByteCheckSame){
    double time0=clock_s();
    bool result;
    if (byteByByteCheckSame)
        result=getFileIsSame(oldZipPath,newZipPath);
    else
        result=getZipIsSame(oldZipPath,newZipPath);
    double time1=clock_s();
    if (result){
        std::cout<<"  check ZipPatch result ok!\n";
        std::cout<<"  check time: "<<(time1-time0)<<" s\n";
    }
    return result;
}

