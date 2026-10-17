// Extra tests for Stack: the hard cases that test_stack.cpp does not cover.
// Added while checking a report that "the stack is not working". These all
// pass on the unchanged Stack.hpp; they stay here so the claim stays checked.
//
// Build and run (from the repository folder):
//     g++ -std=c++17 -Iinclude tests/test_stack_edge.cpp -o test_stack_edge
//     ./test_stack_edge

#include <memory>
#include <stdexcept>
#include <utility>

#include "Stack.hpp"
#include "test_util.hpp"

using namespace opera;

namespace {

// A value whose copy constructor throws on the Nth copy.
struct Fragile {
    static int copies;
    static int failOn;   // -1 = never fail
    int v;
    explicit Fragile(int x) : v(x) {}
    Fragile(const Fragile& o) : v(o.v) {
        if (++copies == failOn) throw std::runtime_error("copy failed");
    }
    Fragile(Fragile&&) = default;
};
int Fragile::copies = 0;
int Fragile::failOn = -1;

// If copying throws halfway, the source must be untouched and nothing leaks.
void testCopyThatThrowsHalfway() {
    Stack<Fragile> a;
    for (int i = 0; i < 5; ++i) a.push(Fragile(i));
    Fragile::copies = 0;
    Fragile::failOn = 3;
    bool threw = false;
    try {
        Stack<Fragile> b(a);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    Fragile::failOn = -1;
    CHECK(threw);
    CHECK_EQ(a.size(), 5u);
    CHECK_EQ(a.top().v, 4);
}

// If copying the value in push throws, the stack must be unchanged.
void testPushThatThrows() {
    Stack<Fragile> a;
    a.push(Fragile(1));
    const Fragile x(9);
    Fragile::copies = 0;
    Fragile::failOn = 1;
    bool threw = false;
    try {
        a.push(x);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    Fragile::failOn = -1;
    CHECK(threw);
    CHECK_EQ(a.size(), 1u);
    CHECK_EQ(a.top().v, 1);
}

// Elements that can only be moved (like std::unique_ptr) must work.
void testMoveOnlyElements() {
    Stack<std::unique_ptr<int>> s;
    s.push(std::make_unique<int>(1));
    s.push(std::make_unique<int>(2));
    const std::unique_ptr<int> p = s.pop();
    CHECK_EQ(*p, 2);
    Stack<std::unique_ptr<int>> t(std::move(s));
    CHECK_EQ(*t.top(), 1);
    CHECK(s.empty());
}

void testSelfMoveAssignment() {
    Stack<int> s;
    s.push(1);
    s.push(2);
    Stack<int>& alias = s;
    s = std::move(alias);
    CHECK_EQ(s.size(), 2u);
    CHECK_EQ(s.pop(), 2);
}

void testEmptyCopies() {
    Stack<int> empty;
    Stack<int> copy(empty);
    CHECK(copy.empty());
    Stack<int> full;
    full.push(5);
    full = empty;
    CHECK(full.empty());
    CHECK_EQ(full.size(), 0u);
}

// Size must not drift over many fill / drain rounds.
void testSizeNeverDrifts() {
    Stack<int> s;
    for (int round = 0; round < 3; ++round) {
        for (int i = 0; i < 100; ++i) s.push(i);
        for (int i = 0; i < 100; ++i) s.pop();
    }
    CHECK(s.empty());
    CHECK_EQ(s.size(), 0u);
}

}  // namespace

int main() {
    testCopyThatThrowsHalfway();
    testPushThatThrows();
    testMoveOnlyElements();
    testSelfMoveAssignment();
    testEmptyCopies();
    testSizeNeverDrifts();
    return report("test_stack_edge");
}
