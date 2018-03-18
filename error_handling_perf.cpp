#include <benchmark/benchmark.h>

#include <vector>
#include <numeric>
#include <iostream>
#include <exception>
#include <cstdlib>

using namespace std;

#define likely(x) __builtin_expect((x), 1)
#define unlikely(x) __builtin_expect((x), 0)

constexpr int pow10(int n) { return n <= 0 ? 1 : 10 * pow10(n - 1); }

template <typename T>
class Expected {
    union {
        T value_;
        exception_ptr error_;
    };
    bool hasValue_;
    Expected() {}

public:
    Expected(const T& val)
        : value_(val)
        , hasValue_(true) {}
    Expected(T&& val)
        : value_(move(val))
        , hasValue_(true) {}
    Expected(const Expected& other)
        : hasValue_(other.hasValue_) {
        if (hasValue_)
            new (&value_) T(other.value_);
        else
            new (&error_) exception_ptr(other.error_);
    }
    Expected(Expected&& other)
        : hasValue_(other.hasValue_) {
        if (hasValue_)
            new (&value_) T(move(other.value_));
        else
            new (&error_) exception_ptr(move(other.error_));
    }
    ~Expected() {}

    template <typename E>
    static Expected<T> fromException(const E& ex) {
        if (typeid(ex) != typeid(E)) {
            throw invalid_argument("invalid exception type; slicing may occur");
        }
        return fromException(make_exception_ptr(ex));
    }
    static Expected<T> fromException(exception_ptr p) {
        Expected<T> res;
        res.hasValue_ = false;
        new (&res.error_) exception_ptr(move(p));
        return res;
    }
    static Expected<T> fromException() { return fromException(current_exception()); }

    bool valid() const { return likely(hasValue_); }

    T& get() {
        if (unlikely(!hasValue_))
            rethrow_exception(error_);
        return value_;
    }
    const T& get() const {
        if (unlikely(!hasValue_))
            rethrow_exception(error_);
        return value_;
    }

    template <typename E>
    bool hasException() const {
        try {
            if (!hasValue_)
                rethrow_exception(error_);
        } catch (const E& ex) {
            return true;
        } catch (...) {
        }
        return false;
    }
};

template <typename T>
class ExpectedEC {
    union {
        T value_;
        uint32_t errorCode_;
    };
    bool hasValue_;
    ExpectedEC() {}

public:
    ExpectedEC(const T& val)
        : value_(val)
        , hasValue_(true) {}
    ExpectedEC(T&& val)
        : value_(move(val))
        , hasValue_(true) {}
    ExpectedEC(const ExpectedEC& other)
        : hasValue_(other.hasValue_) {
        if (hasValue_)
            new (&value_) T(other.value_);
        else
            errorCode_ = other.errorCode_;
    }
    ExpectedEC(ExpectedEC&& other)
        : hasValue_(other.hasValue_) {
        if (hasValue_)
            new (&value_) T(move(other.value_));
        else
            errorCode_ = other.errorCode_;
    }
    ~ExpectedEC() {}

    static ExpectedEC<T> fromErrorCode(uint32_t ec) {
        ExpectedEC<T> res;
        res.hasValue_ = false;
        res.errorCode_ = ec;
        return res;
    }

    bool valid() const { return likely(hasValue_); }

    T& get() {
        if (unlikely(!hasValue_))
            throw runtime_error("unchecked error");
        return value_;
    }
    const T& get() const {
        if (unlikely(!hasValue_))
            throw runtime_error("unchecked error");
        return value_;
    }

    uint32_t getErrorCode() const { return hasValue_ ? 0 : errorCode_; }
};

static constexpr int numLevels = 4;
static constexpr int numSeq = pow10(numLevels);

typedef vector<int> Sequence;

//! Generates a vector of sequences.
//! Each sequences has a set of integers in interval [0, 1024)
//! 'zeroCount' will indicate the number of sequences with length zero
vector<Sequence> genSequences(int zeroCount) {
    srand(0);

    // First generate the sequences to be non-zero
    vector<Sequence> res{(size_t)numSeq};
    for (auto& seq : res) {
        seq.resize(1 + rand() % 20);
        for (auto& el : seq)
            el = rand() % 1024;
    }

    // Now, make some of these sequences to have zero length
    for (int i = 0; i < zeroCount; i++) {
        // Pick a random non-empty sequence
        int seqIdx = 0;
        do {
            seqIdx = rand() % numSeq;
        } while (res[seqIdx].empty());
        // Clear out the elements in the sequence
        res[seqIdx].clear();
    }

    // for (int i = 0; i < numSeq; i++) {
    //     cout << "seq[" << i << "]: ";
    //     for (auto n : res[i]) {
    //         cout << ' ' << n;
    //     }
    //     cout << endl;
    // }
    // exit(1);
    return res;
}

//! Compute the average for the elements in a sequence.
//! Return 0 if the sequence is empty; there is no way for the caller to check if there was an error
int average_ignore(const Sequence& seq) noexcept {
    if (seq.empty())
        return 0;
    auto sum = accumulate(begin(seq), end(seq), 0);
    return sum / int(seq.size());
}

int multilevel_average_ignore(const vector<Sequence>& sequences, int start, int level) noexcept {
    int stride = pow10(level);

    int sum = 0;
    for (int i = 0; i < 10; i++) {
        int idx = start + i * stride;
        int val = level == 0 ? average_ignore(sequences[idx])
                             : multilevel_average_ignore(sequences, idx, level - 1);
        sum += val;
    }
    return sum / 10;
}

//! Compute the average for the elements in a sequence.
//! Uses bool return to indicate errors
bool average_ret(const Sequence& seq, int& res) noexcept {
    if (seq.empty())
        return false;
    auto sum = accumulate(begin(seq), end(seq), 0);
    res = sum / int(seq.size());
    return true;
}

bool multilevel_average_ret(const vector<Sequence>& sequences, int start, int level, int& res) noexcept {
    int stride = pow10(level);

    int sum = 0;
    for (int i = 0; i < 10; i++) {
        int idx = start + i * stride;
        int val = 0;
        bool ok = level == 0 ? average_ret(sequences[idx], val)
                             : multilevel_average_ret(sequences, idx, level - 1, val);
        if (!ok)
            return false;
        sum += val;
    }
    res = sum / 10;
    return true;
}

bool multilevel_average_ret_ign(const vector<Sequence>& sequences, int start, int level, int& res) noexcept {
    int stride = pow10(level);

    int sum = 0;
    for (int i = 0; i < 10; i++) {
        int idx = start + i * stride;
        int val = 0;
        bool ok = level == 0 ? average_ret(sequences[idx], val)
                             : multilevel_average_ret_ign(sequences, idx, level - 1, val);
        if (ok)
            sum += val;
    }
    res = sum / 10;
    return true;
}

//! Compute the average for the elements in a sequence.
//! Throws when the sequence is empty
int average_except(const Sequence& seq) {
    if (seq.empty())
        throw runtime_error("empty sequence");
    auto sum = accumulate(begin(seq), end(seq), 0);
    return sum / int(seq.size());
}

int multilevel_average_except(const vector<Sequence>& sequences, int start, int level) {
    int stride = pow10(level);

    int sum = 0;
    for (int i = 0; i < 10; i++) {
        int idx = start + i * stride;
        int val = level == 0 ? average_except(sequences[idx])
                             : multilevel_average_except(sequences, idx, level - 1);
        sum += val;
    }
    return sum / 10;
}

int multilevel_average_except_ign(const vector<Sequence>& sequences, int start, int level) {
    int stride = pow10(level);

    int sum = 0;
    for (int i = 0; i < 10; i++) {
        int idx = start + i * stride;
        try {
            int val = level == 0 ? average_except(sequences[idx])
                                 : multilevel_average_except_ign(sequences, idx, level - 1);
            sum += val;
        } catch (...) {
        }
    }
    return sum / 10;
}

//! Compute the average for the elements in a sequence.
//! Returns Expected<int>, with possible exceptions
Expected<int> average_expected(const Sequence& seq) {
    if (seq.empty())
        return Expected<int>::fromException(runtime_error("empty sequence"));
    auto sum = accumulate(begin(seq), end(seq), 0);
    return sum / int(seq.size());
}

int multilevel_average_expected(const vector<Sequence>& sequences, int start, int level) {
    int stride = pow10(level);

    int sum = 0;
    for (int i = 0; i < 10; i++) {
        int idx = start + i * stride;
        Expected<int> val = level == 0 ? average_expected(sequences[idx])
                                       : multilevel_average_expected(sequences, idx, level - 1);
        sum += val.get();
    }
    return sum / 10;
}

int multilevel_average_expected_ign(const vector<Sequence>& sequences, int start, int level) {
    int stride = pow10(level);

    int sum = 0;
    for (int i = 0; i < 10; i++) {
        int idx = start + i * stride;
        Expected<int> val = level == 0 ? average_expected(sequences[idx])
                                       : multilevel_average_expected_ign(sequences, idx, level - 1);
        if (likely(val.valid()))
            sum += val.get();
    }
    return sum / 10;
}

//! Compute the average for the elements in a sequence.
//! Returns Expected<int>, with possible exceptions
ExpectedEC<int> average_expectedEC(const Sequence& seq) {
    if (seq.empty())
        return ExpectedEC<int>::fromErrorCode(1);
    auto sum = accumulate(begin(seq), end(seq), 0);
    return sum / int(seq.size());
}

ExpectedEC<int> multilevel_average_expectedEC(
        const vector<Sequence>& sequences, int start, int level) {
    int stride = pow10(level);

    int sum = 0;
    for (int i = 0; i < 10; i++) {
        int idx = start + i * stride;
        ExpectedEC<int> val = level == 0 ? average_expectedEC(sequences[idx])
                                         : multilevel_average_expectedEC(sequences, idx, level - 1);
        if (unlikely(!val.valid()))
            return val;
        sum += val.get();
    }
    return sum / 10;
}

int multilevel_average_expectedEC_ign(const vector<Sequence>& sequences, int start, int level) {
    int stride = pow10(level);

    int sum = 0;
    for (int i = 0; i < 10; i++) {
        int idx = start + i * stride;
        ExpectedEC<int> val =
                level == 0 ? average_expectedEC(sequences[idx])
                           : multilevel_average_expectedEC_ign(sequences, idx, level - 1);
        if (likely(val.valid()))
            sum += val.get();
    }
    return sum / 10;
}

//! Compute the average for the elements in a sequence.
//! Uses passed in error codes
int average_errcode(const Sequence& seq, int& errorCode) noexcept {
    if (seq.empty()) {
        errorCode = 1;
        return int();
    }
    auto sum = accumulate(begin(seq), end(seq), 0);
    return sum / int(seq.size());
}

#define MY_BENCHMARK(funName)                                                                      \
    BENCHMARK(funName) /*->Unit(benchmark::kMicrosecond)*/->Arg(0)->Arg(10)->Arg(100)

static void BM_Ignore(benchmark::State& state) {
    int zeroCount = state.range(0);
    auto sequences = genSequences(zeroCount);
    for (auto _ : state) {
        int res = multilevel_average_ignore(sequences, 0, numLevels - 1);
        benchmark::DoNotOptimize(res);
    }
}
MY_BENCHMARK(BM_Ignore);

static void BM_Ret(benchmark::State& state) {
    int zeroCount = state.range(0);
    auto sequences = genSequences(zeroCount);
    for (auto _ : state) {
        int res = 0;
        bool ok = multilevel_average_ret(sequences, 0, numLevels - 1, res);
        benchmark::DoNotOptimize(res);
        benchmark::DoNotOptimize(ok);
        if (ok != (zeroCount == 0))
            cout << "ERROR found while running stuff";
    }
    // Correctness check
    if (zeroCount == 0) {
        int res = 0;
        bool ok = multilevel_average_ret(sequences, 0, numLevels - 1, res);
        int res2 = multilevel_average_ignore(sequences, 0, numLevels - 1);
        if (!ok || res != res2)
            cout << "ERROR: invalid results" << endl;
    }
}
MY_BENCHMARK(BM_Ret);

static void BM_RetIgn(benchmark::State& state) {
    int zeroCount = state.range(0);
    auto sequences = genSequences(zeroCount);
    for (auto _ : state) {
        int res = 0;
        bool ok = multilevel_average_ret_ign(sequences, 0, numLevels - 1, res);
        benchmark::DoNotOptimize(res);
        benchmark::DoNotOptimize(ok);
        if (!ok)
            cout << "ERROR found while running stuff";
    }
}
MY_BENCHMARK(BM_RetIgn);

static void BM_Except(benchmark::State& state) {
    int zeroCount = state.range(0);
    auto sequences = genSequences(zeroCount);
    for (auto _ : state) {
        try {
            int res = multilevel_average_except(sequences, 0, numLevels - 1);
            benchmark::DoNotOptimize(res);
        } catch (...) {
        }
    }
    // Correctness check
    if (zeroCount == 0) {
        int res = multilevel_average_except(sequences, 0, numLevels - 1);
        int res2 = 0;
        (void)multilevel_average_ret(sequences, 0, numLevels - 1, res2);
        if (res != res2)
            cout << "ERROR: invalid results" << endl;
    }
}
MY_BENCHMARK(BM_Except);

static void BM_ExceptIgn(benchmark::State& state) {
    int zeroCount = state.range(0);
    auto sequences = genSequences(zeroCount);
    for (auto _ : state) {
        try {
            int res = multilevel_average_except_ign(sequences, 0, numLevels - 1);
            benchmark::DoNotOptimize(res);
        } catch (...) {
        }
    }
    // Correctness check
    int res = multilevel_average_except_ign(sequences, 0, numLevels - 1);
    int res2 = 0;
    (void)multilevel_average_ret_ign(sequences, 0, numLevels - 1, res2);
    if (res != res2)
        cout << "ERROR: invalid results" << endl;
}
MY_BENCHMARK(BM_ExceptIgn);

static void BM_Expected(benchmark::State& state) {
    int zeroCount = state.range(0);
    auto sequences = genSequences(zeroCount);
    for (auto _ : state) {
        try {
            int res = multilevel_average_expected(sequences, 0, numLevels - 1);
            benchmark::DoNotOptimize(res);
        } catch (...) {
        }
    }
    // Correctness check
    if (zeroCount == 0) {
        int res = multilevel_average_expected(sequences, 0, numLevels - 1);
        int res2 = multilevel_average_except(sequences, 0, numLevels - 1);
        if (res != res2)
            cout << "ERROR: invalid results" << endl;
    }
}
MY_BENCHMARK(BM_Expected);

static void BM_ExpectedIgn(benchmark::State& state) {
    int zeroCount = state.range(0);
    auto sequences = genSequences(zeroCount);
    for (auto _ : state) {
        try {
            int res = multilevel_average_expected_ign(sequences, 0, numLevels - 1);
            benchmark::DoNotOptimize(res);
        } catch (...) {
        }
    }
    // Correctness check
    int res = multilevel_average_expected_ign(sequences, 0, numLevels - 1);
    int res2 = multilevel_average_except_ign(sequences, 0, numLevels - 1);
    if (res != res2)
        cout << "ERROR: invalid results" << endl;
}
MY_BENCHMARK(BM_ExpectedIgn);

static void BM_ExpectedEC(benchmark::State& state) {
    int zeroCount = state.range(0);
    auto sequences = genSequences(zeroCount);
    for (auto _ : state) {
        try {
            auto res = multilevel_average_expectedEC(sequences, 0, numLevels - 1);
            benchmark::DoNotOptimize(res);
        } catch (...) {
        }
    }
    // Correctness check
    if (zeroCount == 0) {
        int res = multilevel_average_expected(sequences, 0, numLevels - 1);
        auto res2 = multilevel_average_expectedEC(sequences, 0, numLevels - 1);
        if (res != res2.get())
            cout << "ERROR: invalid results" << endl;
    }
}
MY_BENCHMARK(BM_ExpectedEC);

static void BM_ExpectedECIgn(benchmark::State& state) {
    int zeroCount = state.range(0);
    auto sequences = genSequences(zeroCount);
    for (auto _ : state) {
        try {
            int res = multilevel_average_expectedEC_ign(sequences, 0, numLevels - 1);
            benchmark::DoNotOptimize(res);
        } catch (...) {
        }
    }
    // Correctness check
    int res = multilevel_average_expectedEC_ign(sequences, 0, numLevels - 1);
    int res2 = multilevel_average_except_ign(sequences, 0, numLevels - 1);
    if (res != res2)
        cout << "ERROR: invalid results" << endl;
}
MY_BENCHMARK(BM_ExpectedECIgn);

BENCHMARK_MAIN();