#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <exception>

namespace aether {
namespace test {

struct TestInfo {
    std::string suite_name;
    std::string test_name;
    void (*fn)();
};

inline std::vector<TestInfo>& GetTests() {
    static std::vector<TestInfo> tests;
    return tests;
}

inline void RegisterTest(const std::string& suite_name, const std::string& test_name, void (*fn)()) {
    GetTests().push_back({suite_name, test_name, fn});
}

inline int& GetPassCount() { static int count = 0; return count; }
inline int& GetFailCount() { static int count = 0; return count; }

} // namespace test
} // namespace aether

namespace testing {
class Test {
public:
    virtual void SetUp() {}
    virtual void TearDown() {}
    virtual ~Test() = default;
};
} // namespace testing

#define TEST_F(Fixture, TestName) \
    class Fixture##_##TestName : public Fixture { \
    public: \
        void TestBody(); \
        static void RunTest() { \
            Fixture##_##TestName instance; \
            instance.SetUp(); \
            try { \
                instance.TestBody(); \
            } catch (const std::exception& e) { \
                std::cout << "[  FAILED  ] Exception caught: " << e.what() << std::endl; \
                aether::test::GetFailCount()++; \
            } catch (...) { \
                std::cout << "[  FAILED  ] Unknown exception caught" << std::endl; \
                aether::test::GetFailCount()++; \
            } \
            instance.TearDown(); \
        } \
    }; \
    struct Fixture##_##TestName##_Register { \
        Fixture##_##TestName##_Register() { \
            aether::test::RegisterTest(#Fixture, #TestName, &Fixture##_##TestName::RunTest); \
        } \
    } dummy_##Fixture##_##TestName##_register; \
    void Fixture##_##TestName::TestBody()

#define TEST(SuiteName, TestName) \
    class SuiteName##_##TestName { \
    public: \
        void TestBody(); \
        static void RunTest() { \
            SuiteName##_##TestName instance; \
            try { \
                instance.TestBody(); \
            } catch (const std::exception& e) { \
                std::cout << "[  FAILED  ] Exception caught: " << e.what() << std::endl; \
                aether::test::GetFailCount()++; \
            } catch (...) { \
                std::cout << "[  FAILED  ] Unknown exception caught" << std::endl; \
                aether::test::GetFailCount()++; \
            } \
        } \
    }; \
    struct SuiteName##_##TestName##_Register { \
        SuiteName##_##TestName##_Register() { \
            aether::test::RegisterTest(#SuiteName, #TestName, &SuiteName##_##TestName::RunTest); \
        } \
    } dummy_##SuiteName##_##TestName##_register; \
    void SuiteName##_##TestName::TestBody()

// Assertions
#define EXPECT_EQ(val1, val2) \
    if ((val1) != (val2)) { \
        std::cout << __FILE__ << ":" << __LINE__ << " - Expected equality of: " << #val1 << " and " << #val2 \
                  << "\n  Actual: " << (val1) << " vs " << (val2) << std::endl; \
        aether::test::GetFailCount()++; \
    } else { \
        aether::test::GetPassCount()++; \
    }

#define EXPECT_TRUE(val) \
    if (!(val)) { \
        std::cout << __FILE__ << ":" << __LINE__ << " - Expected true for: " << #val << std::endl; \
        aether::test::GetFailCount()++; \
    } else { \
        aether::test::GetPassCount()++; \
    }

#define EXPECT_FALSE(val) \
    if (val) { \
        std::cout << __FILE__ << ":" << __LINE__ << " - Expected false for: " << #val << std::endl; \
        aether::test::GetFailCount()++; \
    } else { \
        aether::test::GetPassCount()++; \
    }
