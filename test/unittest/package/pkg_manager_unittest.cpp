/*
 * Copyright (c) 2021 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <cstring>
#include <fcntl.h>
#include <functional>
#include <gtest/gtest.h>
#include <iostream>
#include <memory>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include "log.h"
#include "pkg_algorithm.h"
#include "pkg_gzipfile.h"
#include "pkg_manager.h"
#include "pkg_manager_impl.h"
#include "pkg_test.h"
#include "pkg_utils.h"
#include "securec.h"

using namespace std;
using namespace hpackage;
using namespace updater;

namespace {
constexpr auto WINDOWBITS = -15;  // 32kb window; negative to indicate a raw stream.
constexpr auto MEMLEVEL = 8;      // the default value.
constexpr auto STRATEGY = Z_DEFAULT_STRATEGY;
constexpr uint16_t HEADER_CRC = 0x02; /* bit 1 set: CRC16 for the gzip header */
constexpr uint16_t EXTRA_FIELD = 0x04; /* bit 2 set: extra field present */
constexpr uint16_t ORIG_NAME = 0x08; /* bit 3 set: original file name present */
constexpr uint16_t COMMENT = 0x10; /* bit 4 set: file comment present */
constexpr uint32_t DEFAULT_LOCAK_DIGEST = 32;
constexpr uint32_t TEST_FILE_VERSION = 1000;
constexpr uint32_t TEST_DECOMPRESS_GZIP_OFFSET = 2;
constexpr int32_t LZ4F_MAX_BLOCKID = 7;
constexpr int32_t ZIP_MAX_LEVEL = 9;

class TestPkgStream : public PkgStreamImpl {
public:
    explicit TestPkgStream(std::string fileName) : PkgStreamImpl(fileName) {}
    virtual ~TestPkgStream() {}

    int32_t Read(const PkgBuffer &buff, size_t start, size_t size, size_t &readLen) override
    {
        return PkgStreamImpl::Read(buff, start, size, readLen);
    }
    int32_t Write(const PkgBuffer &ptr, size_t size, size_t start) override
    {
        return PKG_SUCCESS;
    }
    virtual int32_t Seek(long int sizeT, int whence) override
    {
        return PKG_SUCCESS;
    }
    int32_t Flush(size_t size) override
    {
        return PKG_SUCCESS;
    }
    const std::string GetFileName() const override
    {
        return 0;
    }
    size_t GetFileLength() override
    {
        return 0;
    }
};

class PkgMangerTest : public PkgTest {
public:
    PkgMangerTest() {}
    ~PkgMangerTest() override {}

    static int TestStreamProcess(const PkgBuffer &ptr, size_t size, size_t start, bool isFinish,
        const void *context)
    {
        PKG_LOGI("TestStreamProcess size %zu, start %zu finish %d", size, start, isFinish);
        return PKG_SUCCESS;
    }

    void GetUpgradePkgInfo(UpgradePkgInfo &pkgInfo, std::vector<std::pair<std::string, ComponentInfo>> &files)
    {
        pkgInfo.softwareVersion = "100.100.100.100";
        pkgInfo.date = "2021-02-02";
        pkgInfo.time = "21:23:49";
        pkgInfo.productUpdateId = "555.555.100.555";
        pkgInfo.pkgInfo.entryCount = testFileNames_.size();
        pkgInfo.pkgInfo.digestMethod = PKG_DIGEST_TYPE_SHA256;
        pkgInfo.pkgInfo.signMethod = PKG_SIGN_METHOD_RSA;
        pkgInfo.pkgInfo.pkgType = PKG_PACK_TYPE_UPGRADE;
        pkgInfo.updateFileVersion = TEST_FILE_VERSION;
        std::string filePath;
        uint16_t componentInfoId = 100;
        files.resize(testFileNames_.size());
        for (uint32_t i = 0; i < testFileNames_.size(); i++) {
            filePath = TEST_PATH_FROM;
            filePath += testFileNames_[i].c_str();
            files[i].first = filePath;

            ComponentInfo* info = &files[i].second;
            int ret = BuildFileDigest(*info->digest, sizeof(info->digest), filePath);
            EXPECT_EQ(ret, PKG_SUCCESS);
            info->fileInfo.identity = testFileNames_[i];
            info->fileInfo.unpackedSize = GetFileSize(filePath);
            info->fileInfo.packedSize = info->fileInfo.unpackedSize;
            info->fileInfo.packMethod = PKG_COMPRESS_METHOD_NONE;
            info->fileInfo.digestMethod = PKG_DIGEST_TYPE_SHA256;
            info->version = "2.2.2.2";
            info->id = componentInfoId;
            info->resType = 0;
            info->type = 0;
            info->originalSize = info->fileInfo.unpackedSize;
            info->compFlags = 0;
        }
    }
    int TestPackagePack()
    {
        PKG_LOGI("\n\n ************* TestPackagePack %s \r\n", testPackageName.c_str());
        UpgradePkgInfo pkgInfo;
        std::vector<std::pair<std::string, ComponentInfo>> files;
        GetUpgradePkgInfo(pkgInfo, files);
        std::string packagePath = TEST_PATH_TO;
        packagePath += testPackageName;
        int32_t ret = pkgManager_->CreatePackage(packagePath, GetTestPrivateKeyName(), &pkgInfo.pkgInfo, files);
        EXPECT_EQ(ret, PKG_SUCCESS);
        return 0;
    }

    int TestPackagePackFileNotExist()
    {
        PKG_LOGI("\n\n ************* TestPackagePackFileNotExist %s \r\n", testPackageName.c_str());
        UpgradePkgInfo pkgInfo;
        std::vector<std::pair<std::string, ComponentInfo>> files;
        GetUpgradePkgInfo(pkgInfo, files);
        std::string packagePath = TEST_PATH_TO;
        packagePath += testPackageName;

        // 修改成错误的路径
        files[0].first = "sssssssssss";
        int32_t ret = pkgManager_->CreatePackage(packagePath, GetTestPrivateKeyName(), &pkgInfo.pkgInfo, files);
        EXPECT_EQ(ret, PKG_INVALID_FILE);
        return 0;
    }

    int TestPackagePackParamInvalid()
    {
        PKG_LOGI("\n\n ************* TestPackagePackParamInvalid %s \r\n", testPackageName.c_str());
        std::vector<std::pair<std::string, ComponentInfo>> files;
        std::string packagePath = TEST_PATH_TO;
        packagePath += testPackageName;
        int32_t ret = pkgManager_->CreatePackage(packagePath, GetTestPrivateKeyName(), nullptr, files);
        EXPECT_EQ(ret, PKG_INVALID_PARAM);
        return 0;
    }

    int TestPkgMmmapStream()
    {
        pkgManager_ = static_cast<PkgManagerImpl*>(PkgManager::GetPackageInstance());
        if (pkgManager_ == nullptr) {
            return PKG_SUCCESS;
        }
        PKG_LOGI("\n\n ************* TestCombinePkgUNpack %s \r\n", testCombinePkgName.c_str());
        std::vector<std::string> components;
        int32_t ret = pkgManager_->LoadPackage(TEST_PATH_TO + testCombinePkgName, GetTestCertName(), components);
        if (ret != PKG_SUCCESS) {
            PKG_LOGE("LoadPackage fail ret:%d", ret);
            return ret;
        }

        if (components.size() < 1) {
            return ret;
        }
        // 测试文件提取
        PkgManager::StreamPtr outStream = nullptr;
        const FileInfo *info = pkgManager_->GetFileInfo(components[0]);
        EXPECT_NE(info, nullptr);
        ret = pkgManager_->CreatePkgStream(outStream,
            components[0], info->unpackedSize, PkgStream::PkgStreamType_MemoryMap);
        EXPECT_NE(outStream, nullptr);

        ret = ((MemoryMapStream *)(outStream))->GetStreamType();
        EXPECT_EQ(PkgStream::PkgStreamType_MemoryMap, ret);

        ret = pkgManager_->ExtractFile(components[0], outStream);
        EXPECT_NE(outStream, nullptr);

        MemoryMapStream *memStream = (MemoryMapStream*)outStream;
        constexpr size_t buffSize = 100;
        std::vector<uint8_t> buff(buffSize);
        size_t len = 0;
        PkgBuffer data = {buff};
        ret = memStream->Read(data, 0, buffSize, len);
        EXPECT_EQ(ret, PKG_SUCCESS);

        long int offset = 10;
        ret = memStream->Seek(offset, SEEK_SET);
        EXPECT_EQ(ret, PKG_SUCCESS);
        ret = memStream->Seek(offset, SEEK_CUR);
        EXPECT_EQ(ret, PKG_SUCCESS);
        offset = 0 - offset;
        ret = memStream->Seek(offset, SEEK_END);
        EXPECT_EQ(ret, PKG_SUCCESS);
        ret = memStream->Flush(offset);
        EXPECT_EQ(0, ret);
        ret = memStream->GetBuffer(data);
        EXPECT_EQ(0, ret);
        pkgManager_->ClosePkgStream(outStream);
        return ret;
    }

    int TestPkgStreamImpl()
    {
        std::string path = TEST_PATH_TO + testCombinePkgName;
        std::unique_ptr<TestPkgStream> stream = std::make_unique<TestPkgStream>(path);
        EXPECT_NE(stream, nullptr);
        constexpr size_t buffSize = 10;
        uint8_t buff[buffSize];
        size_t size = sizeof(buff);
        size_t start = 0;
        size_t readLen = 0;
        PkgBuffer buffer(buff, sizeof(buff));
        int ret = ((PkgStreamPtr)(stream.get()))->Read(buffer, start, size, readLen);
        EXPECT_EQ(0, ret);
        PkgBuffer data = {};
        ret = ((PkgStreamPtr)(stream.get()))->GetBuffer(data);
        EXPECT_EQ(0, ret);

        ret = ((PkgStreamPtr)(stream.get()))->GetStreamType();
        EXPECT_EQ(PkgStream::PkgStreamType_Read, ret);

        return 0;
    }

    int TestPkgFileStream()
    {
        pkgManager_ = static_cast<PkgManagerImpl*>(PkgManager::GetPackageInstance());
        if (pkgManager_ == nullptr) {
            return PKG_SUCCESS;
        }
        PKG_LOGI("\n\n ************* TestCombinePkgUNpack %s \r\n", testCombinePkgName.c_str());
        std::vector<std::string> components;
        int32_t ret = pkgManager_->LoadPackage(TEST_PATH_TO + testCombinePkgName, GetTestCertName(), components);
        if (ret != PKG_SUCCESS) {
            PKG_LOGI("LoadPackage fail ret:%d", ret);
            return ret;
        }

        if (components.size() < 1) {
            return ret;
        }
        // 测试文件提取
        PkgManager::StreamPtr outStream = nullptr;
        const FileInfo *info = pkgManager_->GetFileInfo(components[0]);
        EXPECT_NE(info, nullptr);

        ret = pkgManager_->CreatePkgStream(outStream,
            TEST_PATH_TO + components[0], info->unpackedSize, PkgStream::PkgStreamType_Write);
        EXPECT_NE(outStream, nullptr);

        ret = ((FileStream *)(outStream))->GetStreamType();
        EXPECT_EQ(PkgStream::PkgStreamType_Write, ret);

        ret = pkgManager_->CreatePkgStream(outStream,
            TEST_PATH_TO + components[0], info->unpackedSize, PkgStream::PkgStreamType_Write);
        EXPECT_NE(outStream, nullptr);
        ret = pkgManager_->ExtractFile(components[0], outStream);
        EXPECT_NE(outStream, nullptr);
        return ret;
    }

    int TestPkgProcessStream()
    {
        pkgManager_ = static_cast<PkgManagerImpl*>(PkgManager::GetPackageInstance());
        if (pkgManager_ == nullptr) {
            return PKG_SUCCESS;
        }
        PKG_LOGI("\n\n ************* TestCombinePkgUNpack %s \r\n", testCombinePkgName.c_str());
        std::vector<std::string> components;
        int32_t ret = pkgManager_->LoadPackage(TEST_PATH_TO + testCombinePkgName, GetTestCertName(), components);
        if (ret != PKG_SUCCESS) {
            PKG_LOGI("LoadPackage fail ret:%d", ret);
            return ret;
        }

        if (components.size() < 1) {
            return ret;
        }
        // 测试文件提取
        PkgManager::StreamPtr outStream = nullptr;
        const FileInfo *info = pkgManager_->GetFileInfo(components[0]);
        EXPECT_NE(info, nullptr);
        ret = pkgManager_->CreatePkgStream(outStream,
            components[0], TestStreamProcess, this);
        EXPECT_NE(outStream, nullptr);

        ret = ((ProcessorStream *)(outStream))->GetStreamType();
        EXPECT_EQ(PkgStream::PkgStreamType_Process, ret);

        ret = pkgManager_->ExtractFile(components[0], outStream);
        EXPECT_NE(outStream, nullptr);

        size_t len = 0;
        size_t bufferSize = 100;
        long int offset = -10;
        PkgBuffer buffer(bufferSize);
        ret = outStream->Read(buffer, 0, bufferSize, len);
        EXPECT_EQ(ret, PKG_INVALID_STREAM);
        ret = ((ProcessorStream*)outStream)->Seek(offset, SEEK_END);
        EXPECT_EQ(ret, PKG_SUCCESS);
        ret = ((ProcessorStream*)outStream)->Flush(offset);
        EXPECT_EQ(0, ret);
        ret = ((ProcessorStream*)outStream)->GetFileLength();
        EXPECT_EQ(0, ret);
        pkgManager_->ClosePkgStream(outStream);
        return ret;
    }

    int TestInvalidStream()
    {
        std::string packagePath = TEST_PATH_TO;
        packagePath += testPackageName;
        auto stream = std::make_unique<FileStream>(testPackageName, nullptr, 0);
        size_t start = 0;
        size_t readLen = 0;
        size_t bufferSize = 10;
        PkgBuffer buffer(bufferSize);
        int ret = stream->Read(buffer, start, bufferSize, readLen);
        EXPECT_EQ(PKG_INVALID_STREAM, ret);
        return 0;
    }

    int TestRead()
    {
        constexpr size_t buffSize = 8;
        int index = 7;
        uint8_t buffValue = 100;
        uint8_t buff[buffSize] = {0};
        buff[index] = buffValue;
        ReadLE64(buff);
        return 0;
    }

    int TestCheckFile()
    {
        std::string filePath = TEST_PATH_TO;
        filePath += "/4444/";
        int ret = CheckFile(filePath);
        EXPECT_EQ(ret, 0);
        return 0;
    }

    int TestGetCurrPath()
    {
        std::string path = GetCurrPath();
        if (path == "./") {
            EXPECT_EQ(1, 0);
        }
        return 0;
    }

    int TestCreatePackageInvalidFile()
    {
        UpgradePkgInfo pkgInfo;
        size_t testSize = 100;
        uint16_t componentInfoId = 100;
        std::vector<std::pair<std::string, ComponentInfo>> files;
        GetUpgradePkgInfo(pkgInfo, files);
        ComponentInfo info;
        info.fileInfo.identity = "aaaaaaaa";
        info.fileInfo.unpackedSize = testSize;
        info.fileInfo.packedSize = testSize;
        info.fileInfo.packMethod = PKG_COMPRESS_METHOD_NONE;
        info.fileInfo.digestMethod = PKG_DIGEST_TYPE_SHA256;
        info.version = "2.2.2.2";
        info.id = componentInfoId;
        info.resType = 0;
        info.type = 0;
        info.originalSize = testSize;
        info.compFlags = 0;
        std::string packagePath = TEST_PATH_TO;
        packagePath += testPackageName;
        files.push_back(std::pair<std::string, ComponentInfo>("/qqqqqq", info));
        int ret = pkgManager_->CreatePackage(packagePath, GetTestPrivateKeyName(), &pkgInfo.pkgInfo, files);
        EXPECT_EQ(ret, PKG_INVALID_FILE);
        return 0;
    }

    int TestCreatePackageInvalidSignMethod()
    {
        UpgradePkgInfo pkgInfo;
        std::vector<std::pair<std::string, ComponentInfo>> files;
        GetUpgradePkgInfo(pkgInfo, files);
        std::string packagePath = TEST_PATH_TO;
        packagePath += testPackageName;
        uint8_t signMethodIndex = 10;
        pkgInfo.pkgInfo.signMethod = PKG_SIGN_METHOD_RSA + signMethodIndex;
        int ret = pkgManager_->CreatePackage(packagePath, GetTestPrivateKeyName(), &pkgInfo.pkgInfo, files);
        EXPECT_NE(ret, 0);
        return 0;
    }

    int TestLz4PackageInvalidFile()
    {
        pkgManager_ = static_cast<PkgManagerImpl*>(PkgManager::GetPackageInstance());
        EXPECT_NE(pkgManager_, nullptr);

        std::vector<std::pair<std::string, Lz4FileInfo>> files;
        Lz4FileInfo file;
        file.fileInfo.identity = testPackageName;
        file.fileInfo.packMethod = PKG_COMPRESS_METHOD_ZIP;
        file.fileInfo.digestMethod = PKG_DIGEST_TYPE_CRC;
        files.push_back(std::pair<std::string, Lz4FileInfo>("fileName", file));
        PkgInfo pkgInfo;
        pkgInfo.signMethod = PKG_SIGN_METHOD_RSA;
        pkgInfo.digestMethod  = PKG_DIGEST_TYPE_SHA256;
        pkgInfo.pkgType = PKG_PACK_TYPE_GZIP;
        std::string fileName = TEST_PATH_TO;
        fileName += testGZipPackageName;
        int ret = pkgManager_->CreatePackage(fileName, GetTestPrivateKeyName(), &pkgInfo, files);
        EXPECT_EQ(ret, PKG_INVALID_FILE);
        return 0;
    }

    int TestLz4PackageInvalidPkgType()
    {
        pkgManager_ = static_cast<PkgManagerImpl*>(PkgManager::GetPackageInstance());
        EXPECT_NE(pkgManager_, nullptr);

        std::vector<std::pair<std::string, Lz4FileInfo>> files;
        Lz4FileInfo file;
        file.fileInfo.identity = testPackageName;
        file.fileInfo.packMethod = PKG_COMPRESS_METHOD_ZIP;
        file.fileInfo.digestMethod = PKG_DIGEST_TYPE_CRC;
        files.push_back(std::pair<std::string, Lz4FileInfo>("fileName", file));
        PkgInfo pkgInfo;
        pkgInfo.signMethod = PKG_SIGN_METHOD_RSA;
        pkgInfo.digestMethod  = PKG_DIGEST_TYPE_SHA256;
        uint8_t pkgTypeIndex = 100;
        pkgInfo.pkgType = PKG_PACK_TYPE_GZIP + pkgTypeIndex;
        std::string fileName = TEST_PATH_TO;
        fileName += testGZipPackageName;
        int ret = pkgManager_->CreatePackage(fileName, GetTestPrivateKeyName(), &pkgInfo, files);
        EXPECT_EQ(ret, PKG_INVALID_FILE);
        return 0;
    }

    int TestZipPackageInvalidFile()
    {
        pkgManager_ = static_cast<PkgManagerImpl*>(PkgManager::GetPackageInstance());
        EXPECT_NE(pkgManager_, nullptr);

        std::vector<std::pair<std::string, ZipFileInfo>> files;
        for (auto name : testFileNames_) {
            ZipFileInfo file;
            file.fileInfo.identity = name;
            file.fileInfo.packMethod = PKG_COMPRESS_METHOD_ZIP;
            file.fileInfo.digestMethod = PKG_DIGEST_TYPE_CRC;
            files.push_back(std::pair<std::string, ZipFileInfo>("55555555555", file));
        }
        PkgInfo pkgInfo;
        pkgInfo.signMethod = PKG_SIGN_METHOD_RSA;
        pkgInfo.digestMethod  = PKG_DIGEST_TYPE_SHA256;
        pkgInfo.pkgType  = PKG_PACK_TYPE_ZIP;
        std::string fileName = TEST_PATH_TO;
        fileName += testZipPackageName;
        int ret = pkgManager_->CreatePackage(fileName, GetTestPrivateKeyName(), &pkgInfo, files);
        EXPECT_EQ(ret, PKG_INVALID_FILE);
        return 0;
    }

    int TestLoadPackageFail()
    {
        pkgManager_ = static_cast<PkgManagerImpl*>(PkgManager::GetPackageInstance());
        EXPECT_NE(pkgManager_, nullptr);
        std::vector<std::string> components;
        std::string fileName = TEST_PATH_TO;
        fileName += "testZipPackageName.aaa";
        int32_t ret = pkgManager_->LoadPackage(fileName, GetTestCertName(), components);
        EXPECT_EQ(ret, PKG_INVALID_FILE);
        return 0;
    }

    void TestDecompressLz4plus(hpackage::Lz4FileInfo &lz4Info)
    {
        pkgManager_ = static_cast<PkgManagerImpl*>(PkgManager::GetPackageInstance());
        EXPECT_NE(pkgManager_, nullptr);
        int8_t compressionLevel = 2;
        lz4Info.fileInfo.identity = "Lz4";
        lz4Info.fileInfo.packMethod = PKG_COMPRESS_METHOD_LZ4;
        lz4Info.fileInfo.digestMethod = PKG_DIGEST_TYPE_NONE;
        lz4Info.compressionLevel = compressionLevel;
        lz4Info.blockSizeID = 0;
        lz4Info.contentChecksumFlag = 0;
        lz4Info.blockIndependence = 0;
    }

    int TestDecompressLz4(hpackage::Lz4FileInfo &lz4Info,
        std::vector<uint8_t> &uncompressedData, std::vector<uint8_t> &digest)
    {
        std::string testFileName = TEST_PATH_FROM + "../diffpatch/PatchLz4test_new.lz4";
        size_t fileSize = GetFileSize(testFileName);
        int32_t fd = open(testFileName.c_str(), O_RDWR);
        PKG_CHECK(fd > 0, return -1, "Can not open file ");

        size_t uncompressedDataSize = 1024;
        uncompressedData.resize(uncompressedDataSize);
        PkgManager::StreamPtr stream = nullptr;
        pkgManager_->CreatePkgStream(stream, "Lz4",
            [&](const PkgBuffer &buffer, size_t size, size_t start, bool isFinish, const void* context) -> int {
                (void)isFinish;
                (void)context;
                (void)size;
                (void)start;
                (void)buffer;
                size_t oldSize = uncompressedData.size();
                if ((start + size) > uncompressedData.size()) {
                    uncompressedData.resize(oldSize * ((start + size) / oldSize + 1));
                }
                EXPECT_GE(memcpy_s(uncompressedData.data() + start, size, buffer.buffer, size), 0);
                return PKG_SUCCESS;
            }, nullptr);

        std::unique_ptr<hpackage::PkgStream, std::function<void(hpackage::PkgManager::StreamPtr)>> outStream(stream,
            [&](hpackage::PkgManager::StreamPtr stream) {
            pkgManager_->ClosePkgStream(stream);
        });
        PKG_CHECK(outStream != nullptr, return -1, "Can not create stream ");

        void* mappedData = mmap(nullptr, (size_t)fileSize, PROT_READ, MAP_SHARED, fd, 0);
        PKG_CHECK(mappedData != MAP_FAILED, close(fd); return -2, "Can not mmap ");

        size_t addrOffset = 4;
        TestDecompressLz4plus(lz4Info);
        hpackage::PkgBuffer buffer(static_cast<uint8_t*>(mappedData) + addrOffset, fileSize);
        int32_t ret = pkgManager_->DecompressBuffer(&lz4Info.fileInfo, buffer, outStream.get());

        // 生成摘要，检查数据完整
        SHA256_CTX sha256Ctx = {};
        SHA256_Init(&sha256Ctx);
        SHA256_Update(&sha256Ctx, static_cast<const uint8_t*>(mappedData), lz4Info.fileInfo.packedSize + 4);
        SHA256_Final(digest.data(), &sha256Ctx);

        munmap(mappedData, fileSize);
        close(fd);
        PKG_CHECK(ret == 0, return -1, "Can not decompress buff ");
        PKG_LOGE("GetLz4UncompressedData packedSize:%zu unpackedSize:%zu fileSize: %zu",
            lz4Info.fileInfo.packedSize, lz4Info.fileInfo.unpackedSize, fileSize);
        return 0;
    }

    void TestDecompressGzipInitFile(hpackage::ZipFileInfo &zipInfo, size_t &offset,
        size_t &fileSize, void *mappedData)
    {
        int32_t zipMethod = 8;
        int32_t zipLevel = 4;
        zipInfo.fileInfo.identity = "gzip";
        zipInfo.fileInfo.packMethod = PKG_COMPRESS_METHOD_GZIP;
        zipInfo.fileInfo.digestMethod = PKG_DIGEST_TYPE_NONE;
        zipInfo.method = zipMethod;
        zipInfo.level = zipLevel;
        zipInfo.memLevel = MEMLEVEL;
        zipInfo.windowBits = WINDOWBITS;
        zipInfo.strategy = STRATEGY;

        auto buffer = reinterpret_cast<uint8_t*>(mappedData);
        auto header = reinterpret_cast<GZipHeader*>(mappedData);
        // 有扩展头信息
        if (header->flags & EXTRA_FIELD) {
            uint16_t extLen = ReadLE16(buffer + offset);
            offset += sizeof(uint16_t) + extLen;
        }
        if (header->flags & ORIG_NAME) {
            std::string fileName;
            PkgFile::ConvertBufferToString(fileName, {buffer + offset, fileSize - offset});
            offset += fileName.size() + 1;
        }
        if (header->flags & COMMENT) {
            std::string comment;
            PkgFile::ConvertBufferToString(comment, {buffer + offset, fileSize - offset});
            offset += comment.size() + 1;
        }
        if (header->flags & HEADER_CRC) { // 暂不校验
            offset += TEST_DECOMPRESS_GZIP_OFFSET;
        }
        return;
    }

    int TestDecompressGzip(hpackage::ZipFileInfo &zipInfo, std::vector<uint8_t> &uncompressedData,
        std::vector<uint8_t> &digest)
    {
        std::string testFileName = TEST_PATH_FROM + "../applypatch/TestDecompressGzip.new.gz";
        size_t fileSize = GetFileSize(testFileName);
        size_t uncompressedDataSize = 1024;
        int32_t fd = open(testFileName.c_str(), O_RDWR);
        EXPECT_GT(fd, 0);

        uncompressedData.resize(uncompressedDataSize);
        PkgManager::StreamPtr stream = nullptr;
        pkgManager_->CreatePkgStream(stream, "Gzip",
            [&](const PkgBuffer &buffer, size_t size, size_t start, bool isFinish, const void* context) -> int {
                (void)isFinish;
                (void)context;
                (void)size;
                (void)start;
                (void)buffer;
                size_t oldSize = uncompressedData.size();
                if ((start + size) > uncompressedData.size()) {
                    uncompressedData.resize(oldSize * ((start + size) / oldSize + 1));
                }
                EXPECT_GE(memcpy_s(uncompressedData.data() + start, size, buffer.buffer, size), 0);
                return PKG_SUCCESS;
            }, nullptr);

        std::unique_ptr<hpackage::PkgStream, std::function<void(hpackage::PkgManager::StreamPtr)>> outStream(stream,
            [&](hpackage::PkgManager::StreamPtr stream) {
            pkgManager_->ClosePkgStream(stream);
        });
        PKG_CHECK(outStream != nullptr, return -1, "Can not create stream ");

        void* mappedData = mmap(nullptr, (size_t)fileSize, PROT_READ, MAP_SHARED, fd, 0);
        PKG_CHECK(mappedData != MAP_FAILED, close(fd); return -2, "Can not mmap ");

        pkgManager_ = static_cast<PkgManagerImpl*>(PkgManager::GetPackageInstance());
        EXPECT_NE(pkgManager_, nullptr);
        size_t offset = 10;
        TestDecompressGzipInitFile(zipInfo, offset, fileSize, mappedData);

        hpackage::PkgBuffer data(reinterpret_cast<uint8_t*>(mappedData) + offset, fileSize);
        int32_t ret = pkgManager_->DecompressBuffer(&zipInfo.fileInfo, data, outStream.get());

        // 生成摘要，检查数据完整
        SHA256_CTX sha256Ctx = {};
        SHA256_Init(&sha256Ctx);
        SHA256_Update(&sha256Ctx, reinterpret_cast<const uint8_t*>(mappedData) + offset, zipInfo.fileInfo.packedSize);
        SHA256_Final(digest.data(), &sha256Ctx);

        munmap(mappedData, fileSize);
        close(fd);
        PKG_CHECK(ret == 0, return -1, "Can not decompress buff ");
        PKG_LOGE("GetGZipUncompressedData packedSize:%zu unpackedSize:%zu",
            zipInfo.fileInfo.packedSize, zipInfo.fileInfo.unpackedSize);

        return 0;
    }

    int TestCompressBuffer(hpackage::FileInfo &info, std::vector<uint8_t> uncompressedData,
        std::vector<uint8_t> digest)
    {
        pkgManager_ = static_cast<PkgManagerImpl*>(PkgManager::GetPackageInstance());
        EXPECT_NE(pkgManager_, nullptr);
        // 生成摘要，检查数据完整
        SHA256_CTX sha256Ctx = {};
        SHA256_Init(&sha256Ctx);
        PkgManager::StreamPtr stream = nullptr;
        pkgManager_->CreatePkgStream(stream, "Gzip",
            [&](const PkgBuffer &ptr, size_t size, size_t start, bool isFinish, const void* context) -> int {
                (void)isFinish;
                (void)context;
                (void)size;
                (void)start;
                (void)ptr;
                SHA256_Update(&sha256Ctx, ptr.buffer, size);
                return PKG_SUCCESS;
            }, nullptr);

        std::unique_ptr<hpackage::PkgStream, std::function<void(hpackage::PkgManager::StreamPtr)>> outStream(stream,
            [&](hpackage::PkgManager::StreamPtr stream) {
            pkgManager_->ClosePkgStream(stream);
        });
        PKG_CHECK(outStream != nullptr, return -1, "Can not create stream ");
        hpackage::PkgBuffer buffer(uncompressedData.data(), info.unpackedSize);
        int32_t ret = pkgManager_->CompressBuffer(&info, buffer, outStream.get());
        PKG_LOGE("GetGZipUncompressedData packedSize:%zu unpackedSize:%zu",
            info.packedSize, info.unpackedSize);
        PKG_CHECK(ret == 0, return -1, "Fail to CompressBuffer");
        std::vector<uint8_t> localDigest(DEFAULT_LOCAK_DIGEST);
        SHA256_Final(localDigest.data(), &sha256Ctx);
        ret = memcmp(localDigest.data(), digest.data(), localDigest.size());
        PKG_LOGE("digest cmp result %d", ret);
        return ret;
    }
};

TEST_F(PkgMangerTest, TestGZipBuffer)
{
    PkgMangerTest test;
    hpackage::ZipFileInfo zipInfo;
    std::vector<uint8_t> digest(32);
    std::vector<uint8_t> uncompressedData;
    EXPECT_EQ(0, test.TestDecompressGzip(zipInfo, uncompressedData, digest));
    int32_t ret = 0;
    for (int32_t i = 0; i < ZIP_MAX_LEVEL; i++) {
        zipInfo.level = i;
        ret = test.TestCompressBuffer(zipInfo.fileInfo, uncompressedData, digest);
        if (ret == 0) {
            break;
        }
    }
    EXPECT_EQ(0, ret);
    uncompressedData.clear();
}

TEST_F(PkgMangerTest, TestLz4Buffer)
{
    PkgMangerTest test;
    hpackage::Lz4FileInfo lz4Info;
    std::vector<uint8_t> digest(32);
    std::vector<uint8_t> uncompressedData;
    EXPECT_EQ(0, test.TestDecompressLz4(lz4Info, uncompressedData, digest));
    int32_t ret = 0;
    for (int32_t i = 0; i < LZ4F_MAX_BLOCKID; i++) {
        lz4Info.compressionLevel = 2;
        lz4Info.blockSizeID = i;
        ret = test.TestCompressBuffer(lz4Info.fileInfo, uncompressedData, digest);
        if (ret == 0) {
            break;
        }
    }
    EXPECT_EQ(0, ret);
    uncompressedData.clear();
}

TEST_F(PkgMangerTest, TestInvalidCreatePackage)
{
    PkgMangerTest test;
    EXPECT_EQ(0, test.TestPackagePack());
    EXPECT_EQ(0, test.TestPackagePackFileNotExist());
    EXPECT_EQ(0, test.TestPackagePackParamInvalid());
}

TEST_F(PkgMangerTest, TestPkgStreamImpl)
{
    PkgMangerTest test;
    EXPECT_EQ(0, test.TestPkgStreamImpl());
}

TEST_F(PkgMangerTest, TestPkgMmmapStream)
{
    PkgMangerTest test;
    EXPECT_EQ(0, test.TestPkgMmmapStream());
}

TEST_F(PkgMangerTest, TestPkgFileStream)
{
    PkgMangerTest test;
    EXPECT_EQ(0, test.TestPkgFileStream());
}

TEST_F(PkgMangerTest, TestPkgProcessStream)
{
    PkgMangerTest test;
    EXPECT_EQ(0, test.TestPkgProcessStream());
}

TEST_F(PkgMangerTest, TestInvalidStream)
{
    PkgMangerTest test;
    EXPECT_EQ(0, test.TestInvalidStream());
}

TEST_F(PkgMangerTest, TestRead)
{
    PkgMangerTest test;
    EXPECT_EQ(0, test.TestRead());
}

TEST_F(PkgMangerTest, TestCheckFile)
{
    PkgMangerTest test;
    EXPECT_EQ(0, test.TestCheckFile());
}

TEST_F(PkgMangerTest, TestCreatePackageFail)
{
    PkgMangerTest test;
    EXPECT_EQ(0, test.TestCreatePackageInvalidFile());
    EXPECT_EQ(0, test.TestCreatePackageInvalidSignMethod());
    EXPECT_EQ(0, test.TestLz4PackageInvalidFile());
    EXPECT_EQ(0, test.TestLz4PackageInvalidPkgType());
    EXPECT_EQ(0, test.TestZipPackageInvalidFile());
}

TEST_F(PkgMangerTest, TestLoadPackageFail)
{
    PkgMangerTest test;
    EXPECT_EQ(0, test.TestLoadPackageFail());
}
}
