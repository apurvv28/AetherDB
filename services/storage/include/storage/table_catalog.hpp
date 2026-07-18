#pragma once

#include "storage/schema.hpp"
#include "common/types.hpp"
#include <string>
#include <unordered_map>
#include <utility>

namespace aether {

struct TableMetadata {
    std::string name;
    Schema schema;
    page_id_t first_page_id;
    page_id_t index_root_id;
};

class TableCatalog {
public:
    TableCatalog() = default;

    void CreateTable(std::string name, Schema schema, page_id_t first_page_id, page_id_t index_root_id = INVALID_PAGE_ID) {
        tables_[name] = {name, std::move(schema), first_page_id, index_root_id};
    }

    bool GetTableMetadata(const std::string &name, TableMetadata *meta) const {
        auto iter = tables_.find(name);
        if (iter == tables_.end()) {
            return false;
        }
        *meta = iter->second;
        return true;
    }

    void UpdateIndexRoot(const std::string &name, page_id_t new_root) {
        auto iter = tables_.find(name);
        if (iter != tables_.end()) {
            iter->second.index_root_id = new_root;
        }
    }

private:
    std::unordered_map<std::string, TableMetadata> tables_;
};

} // namespace aether
