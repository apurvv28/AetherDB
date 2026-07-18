#pragma once

#include "storage/schema.hpp"
#include <vector>
#include <string>
#include <cstring>
#include <initializer_list>

namespace aether {

class Value {
public:
    Value() : type_(TypeId::INVALID), int_val_(0), bool_val_(false) {}
    explicit Value(int val) : type_(TypeId::INT), int_val_(val), bool_val_(false) {}
    explicit Value(int64_t val) : type_(TypeId::INT), int_val_(val), bool_val_(false) {}
    explicit Value(std::string val) : type_(TypeId::VARCHAR), int_val_(0), str_val_(std::move(val)), bool_val_(false) {}
    explicit Value(const char* val) : type_(TypeId::VARCHAR), int_val_(0), str_val_(val), bool_val_(false) {}
    explicit Value(bool val) : type_(TypeId::BOOLEAN), int_val_(0), bool_val_(val) {}

    TypeId GetType() const { return type_; }
    int64_t GetAsInt() const { return int_val_; }
    const std::string &GetAsVarChar() const { return str_val_; }
    bool GetAsBool() const { return bool_val_; }

    void Serialize(char *dest) const {
        if (type_ == TypeId::INT) {
            std::memcpy(dest, &int_val_, 8);
        } else if (type_ == TypeId::BOOLEAN) {
            char b = bool_val_ ? 1 : 0;
            std::memcpy(dest, &b, 1);
        } else if (type_ == TypeId::VARCHAR) {
            uint32_t len = str_val_.size();
            std::memcpy(dest, &len, 4);
            std::memcpy(dest + 4, str_val_.data(), len);
        }
    }

    void Deserialize(const char *src, TypeId type) {
        type_ = type;
        if (type == TypeId::INT) {
            std::memcpy(&int_val_, src, 8);
        } else if (type == TypeId::BOOLEAN) {
            char b;
            std::memcpy(&b, src, 1);
            bool_val_ = (b != 0);
        } else if (type == TypeId::VARCHAR) {
            uint32_t len;
            std::memcpy(&len, src, 4);
            str_val_.assign(src + 4, len);
        }
    }

    uint32_t GetSerializedLength() const {
        if (type_ == TypeId::INT) return 8;
        if (type_ == TypeId::BOOLEAN) return 1;
        if (type_ == TypeId::VARCHAR) return 4 + str_val_.size();
        return 0;
    }

private:
    TypeId type_;
    int64_t int_val_;
    std::string str_val_;
    bool bool_val_;
};

class Tuple {
public:
    Tuple() = default;
    explicit Tuple(std::vector<Value> values) : values_(std::move(values)) {}
    Tuple(std::initializer_list<Value> values) : values_(values) {}

    const std::vector<Value> &GetValues() const { return values_; }
    const Value &GetValue(uint32_t idx) const { return values_[idx]; }

    void Serialize(char *dest) const {
        uint32_t offset = 0;
        for (const auto &val : values_) {
            val.Serialize(dest + offset);
            offset += val.GetSerializedLength();
        }
    }

    void Deserialize(const char *src, const Schema &schema) {
        values_.clear();
        uint32_t offset = 0;
        for (uint32_t i = 0; i < schema.GetColumnCount(); ++i) {
            const auto &col = schema.GetColumn(i);
            Value val;
            val.Deserialize(src + offset, col.GetType());
            offset += val.GetSerializedLength();
            values_.push_back(std::move(val));
        }
    }

    uint32_t GetSerializedLength() const {
        uint32_t length = 0;
        for (const auto &val : values_) {
            length += val.GetSerializedLength();
        }
        return length;
    }

private:
    std::vector<Value> values_;
};

} // namespace aether
