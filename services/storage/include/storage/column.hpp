#pragma once

#include <string>
#include <cstdint>
#include <utility>

namespace aether {

enum class TypeId { INVALID = 0, INT = 1, VARCHAR = 2, BOOLEAN = 3 };

class Column {
public:
    Column() : type_(TypeId::INVALID), length_(0) {}
    
    Column(std::string name, TypeId type, uint32_t length = 0)
        : name_(std::move(name)), type_(type), length_(length) {
        if (type == TypeId::INT) length_ = 8;
        else if (type == TypeId::BOOLEAN) length_ = 1;
    }

    const std::string &GetName() const { return name_; }
    TypeId GetType() const { return type_; }
    uint32_t GetLength() const { return length_; }
    bool IsVariableLength() const { return type_ == TypeId::VARCHAR; }

private:
    std::string name_;
    TypeId type_;
    uint32_t length_;
};

} // namespace aether
