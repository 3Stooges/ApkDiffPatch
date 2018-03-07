//  normalized.cpp
//  ApkNormalized
/*
 The MIT License (MIT)
 Copyright (c) 2016-2018 HouSisong
 
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
#include "normalized.h"
#include "../patch/Zipper.h"

#define  check(value) { \
    if (!(value)){ printf(#value" ERROR!\n");  \
        result=false; if (!_isInClear){ goto clear; } } }

bool ZipNormalized(const char* srcApk,const char* dstApk){
    bool result=true;
    bool _isInClear=false;
    int fileCount=0;
    UnZipper unzipper;
    Zipper   zipper;
    UnZipper_init(&unzipper);
    Zipper_init(&zipper);
    
    check(UnZipper_openRead(&unzipper,srcApk));
    fileCount=UnZipper_fileCount(&unzipper);
    check(Zipper_openWrite(&zipper,dstApk,fileCount));
    
    for (int i=0; i<fileCount; ++i) {
        if (UnZipper_file_isCompressed(&unzipper,i)) {
            ZipFilePos_t uncompressedSize=UnZipper_file_uncompressedSize(&unzipper,i);
            assert(uncompressedSize==(size_t)uncompressedSize);
            check(Zipper_file_append_begin(&zipper,&unzipper,i,uncompressedSize,false));
            const hpatch_TStreamOutput* stream=Zipper_file_append_part_as_stream(&zipper);
            check(stream!=0);
            check(UnZipper_fileData_decompressTo(&unzipper,i,stream));
            check(Zipper_file_append_end(&zipper));
        }else{
            check(Zipper_file_append_copy(&zipper,&unzipper,i));
        }
    }
    
    check(Zipper_addApkNormalizedTag_before_apkV2Sign(&zipper));
    //no run Zipper_copyApkV2Sign_before_fileHeader
    for (int i=0; i<fileCount; ++i) {
        check(Zipper_fileHeader_append(&zipper,&unzipper,i));
    }
    check(Zipper_endCentralDirectory_append(&zipper,&unzipper));
clear:
    _isInClear=true;
    check(UnZipper_close(&unzipper));
    check(Zipper_close(&zipper));
    return result;
}