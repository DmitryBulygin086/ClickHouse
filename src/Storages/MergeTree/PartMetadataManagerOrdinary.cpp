#include "PartMetadataManagerOrdinary.h"

#include <IO/ReadBufferFromFileBase.h>
#include <Compression/CompressedReadBufferFromFile.h>
#include <Disks/IDisk.h>
#include <Storages/MergeTree/IMergeTreeDataPart.h>

namespace DB
{

std::unique_ptr<ReadBuffer> PartMetadataManagerOrdinary::read(const String & file_name) const
{
    constexpr size_t size_hint = 4096; /// These files are small.
    auto res = part->getDataPartStorage().readFile(file_name, getReadSettings().adjustBufferSize(size_hint), size_hint, std::nullopt);

    if (isCompressedFromFileName(file_name))
        return std::make_unique<CompressedReadBufferFromFile>(std::move(res));

    return res;
}

std::unique_ptr<ReadBuffer> PartMetadataManagerOrdinary::readIfExists(const String & file_name) const
{
    constexpr size_t size_hint = 4096;  /// These files are small.
    if (auto res = part->getDataPartStorage().readFileIfExists(file_name, ReadSettings().adjustBufferSize(size_hint), size_hint, std::nullopt))
    {
        if (isCompressedFromFileName(file_name))
            return std::make_unique<CompressedReadBufferFromFile>(std::move(res));

        return res;
    }
    return {};
}

bool PartMetadataManagerOrdinary::exists(const String & file_name) const
{
    return part->getDataPartStorage().existsFile(file_name);
}

}
