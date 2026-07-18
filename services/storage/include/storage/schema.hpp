#pragma once

#include "storage/column.hpp"
#include <vector>
#include <stdexcept>

namespace aether {

class Schema {
public:
    Schema() = default;
    
    explicit Schema(std::vector<Column> columns) : columns_(std::move(columns)) {}

    const std::vector<Column> &GetColumns() const { return columns_; }
    
    const Column &GetColumn(uint32_t col_idx) const {
        if (col_idx >= columns_.size()) {
            throw std::out_of_range("Column index out of range");
        }
        return columns_[col_idx];
    }
    
    uint32_t GetColumnCount() const { return columns_.size(); }

    int GetColIdx(const std::string &col_name) const {
        for (size_t i = 0; i < columns_.size(); ++i) {
            if (columns_[i].GetName() == col_name) {
                return i;
            }
        }
        return -1;
    }

private:
    std::vector<Column> columns_;
};

} // namespace aether
