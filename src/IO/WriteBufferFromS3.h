#pragma once

#include "config.h"

#if USE_AWS_S3

#include <memory>
#include <vector>
#include <list>

#include <base/types.h>
#include <Common/logger_useful.h>
#include <Common/ThreadPool.h>
#include <IO/BufferWithOwnMemory.h>
#include <IO/WriteBuffer.h>
#include <IO/WriteSettings.h>
#include <Storages/StorageS3Settings.h>
#include <Interpreters/threadPoolCallbackRunner.h>

#include <aws/core/utils/memory/stl/AWSStringStream.h>


namespace Aws::S3
{
class S3Client;
}

namespace Aws::S3::Model
{
    class UploadPartRequest;
    class PutObjectRequest;
}

namespace DB
{

class WriteBufferFromFile;

/**
 * Buffer to write a data to a S3 object with specified bucket and key.
 * If data size written to the buffer is less than 'max_single_part_upload_size' write is performed using singlepart upload.
 * In another case multipart upload is used:
 * Data is divided on chunks with size greater than 'minimum_upload_part_size'. Last chunk can be less than this threshold.
 * Each chunk is written as a part to S3.
 */
class WriteBufferFromS3 final : public WriteBuffer
{
public:
    WriteBufferFromS3(
        std::shared_ptr<const Aws::S3::S3Client> client_ptr_,
        const String & bucket_,
        const String & key_,
        const S3Settings::RequestSettings & request_settings_,
        std::optional<std::map<String, String>> object_metadata_ = std::nullopt,
        size_t buffer_size_ = DBMS_DEFAULT_BUFFER_SIZE,
        ThreadPoolCallbackRunner<void> schedule_ = {},
        const WriteSettings & write_settings_ = {});

    ~WriteBufferFromS3() override;

    void nextImpl() override;

    void preFinalize() override;

    struct Memory : public Aws::StringStream::ExternalMemory
    {
        Memory() = default;
        ~Memory() override;

        Allocator<false, false> allocator;
    };

private:
    std::shared_ptr<Aws::StringStream> allocateBuffer();

    void createMultipartUpload();
    void writePart(std::shared_ptr<Aws::StringStream> temporary_buffer);
    void completeMultipartUpload();

    void makeSinglepartUpload(std::shared_ptr<Aws::StringStream> temporary_buffer);

    /// Receives response from the server after sending all data.
    void finalizeImpl() override;

    struct UploadPartTask;
    void fillUploadRequest(Aws::S3::Model::UploadPartRequest & req, std::shared_ptr<Aws::StringStream> temporary_buffer);
    void processUploadRequest(UploadPartTask & task);

    struct PutObjectTask;
    void fillPutRequest(Aws::S3::Model::PutObjectRequest & req, std::shared_ptr<Aws::StringStream> temporary_buffer);
    void processPutRequest(const PutObjectTask & task);

    void waitForReadyBackGroundTasks();
    void waitForAllBackGroundTasks();
    void waitForAllBackGroundTasksUnlocked(std::unique_lock<std::mutex> & bg_tasks_lock);

    std::unique_ptr<Memory> memory;

    const String bucket;
    const String key;
    const S3Settings::RequestSettings request_settings;
    const std::shared_ptr<const Aws::S3::S3Client> client_ptr;
    const std::optional<std::map<String, String>> object_metadata;

    size_t upload_part_size = 0;
    size_t part_number = 0;

    /// Upload in S3 is made in parts.
    /// We initiate upload, then upload each part and get ETag as a response, and then finalizeImpl() upload with listing all our parts.
    String multipart_upload_id;
    std::vector<String> TSA_GUARDED_BY(bg_tasks_mutex) part_tags;

    bool is_prefinalized = false;

    /// Following fields are for background uploads in thread pool (if specified).
    /// We use std::function to avoid dependency of Interpreters
    const ThreadPoolCallbackRunner<void> schedule;

    std::unique_ptr<PutObjectTask> put_object_task; /// Does not need protection by mutex because of the logic around is_finished field.
    std::list<UploadPartTask> TSA_GUARDED_BY(bg_tasks_mutex) upload_object_tasks;
    int num_added_bg_tasks TSA_GUARDED_BY(bg_tasks_mutex) = 0;
    int num_finished_bg_tasks TSA_GUARDED_BY(bg_tasks_mutex) = 0;

    std::mutex bg_tasks_mutex;
    std::condition_variable bg_tasks_condvar;

    Poco::Logger * log = &Poco::Logger::get("WriteBufferFromS3");

    WriteSettings write_settings;
};

}

#endif
