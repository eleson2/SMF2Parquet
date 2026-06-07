#pragma once
/*
 * sinks/subtype_router_sink.h — route stub types to subtype-specific tables.
 * Used for types where we haven't implemented bespoke parsers but want
 * consistent smfNN-S naming for all captured subtypes.
 */

#include "header_only_sink.h"
#include <map>
#include <memory>
#include <string>

namespace s2p {

class SubtypeRouterSink {
public:
    SubtypeRouterSink(std::string type_prefix, std::string customer, std::string out_root,
                      std::string run_id, std::string source_file, int64_t ingest_us)
        : prefix_(std::move(type_prefix)), customer_(std::move(customer)),
          out_root_(std::move(out_root)), run_id_(std::move(run_id)),
          source_(std::move(source_file)), ingest_(ingest_us) {}

    void write(const mf::SmfHeader& h, uint16_t subtype) {
        std::string table_name = prefix_;
        if (subtype > 0) {
            table_name += "-" + std::to_string(subtype);
        }
        
        auto it = sinks_.find(subtype);
        if (it == sinks_.end()) {
            it = sinks_.emplace(subtype, std::make_unique<HeaderOnlySink>(
                table_name, customer_, out_root_, run_id_, source_, ingest_)).first;
        }
        it->second->write(h, subtype);
    }

    void close() {
        for (auto& [st, sink] : sinks_) {
            sink->close();
        }
    }

    uint64_t rows() const noexcept {
        uint64_t total = 0;
        for (const auto& [st, sink] : sinks_) {
            total += sink->rows();
        }
        return total;
    }

private:
    std::string prefix_, customer_, out_root_, run_id_, source_;
    int64_t ingest_;
    std::map<uint16_t, std::unique_ptr<HeaderOnlySink>> sinks_;
};

} // namespace s2p
