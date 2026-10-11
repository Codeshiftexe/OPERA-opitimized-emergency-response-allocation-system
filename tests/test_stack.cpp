// Tests for Stack.
//
// Build and run:
//     g++ -std=c++17 -Iinclude tests/test_stack.cpp -o test_stack
//     ./test_stack
//


#include <stdexcept>
#include <string>
#include <utility>

#include "Stack.hpp"
#include "test_util.hpp"

using namespace opera;

template <typename F>
static bool throwsOutOfRange(F f) {
    try {
        f();
    } catch (const std::out_of_range&) {
        return true;
    }
    return false;
}

static void testEmptyStack() {
    Stack<int> s;
    CHECK(s.empty());
    CHECK_EQ(s.size(), 0u);
    CHECK(throwsOutOfRange([&] { s.pop(); }));
    CHECK(throwsOutOfRange([&] { s.top(); }));
}

static void testLastInFirstOut() {
    Stack<int> s;
    for (int i = 1; i <= 5; ++i) {
        s.push(i);
    }
    CHECK_EQ(s.size(), 5u);
    for (int want = 5; want >= 1; --want) {
        CHECK_EQ(s.pop(), want);
    }
    CHECK(s.empty());
}

static void testTopDoesNotRemove() {
    Stack<int> s;
    s.push(10);
    s.push(20);
    CHECK_EQ(s.top(), 20);
    CHECK_EQ(s.size(), 2u);

    s.top() = 25;
    CHECK_EQ(s.pop(), 25);
    CHECK_EQ(s.top(), 10);
}

static void testSizeThroughInterleavedOps() {
    Stack<int> s;
    s.push(1);
    s.push(2);
    s.pop();
    s.push(3);
    s.push(4);
    CHECK_EQ(s.size(), 3u);
    CHECK_EQ(s.pop(), 4);
    CHECK_EQ(s.pop(), 3);
    CHECK_EQ(s.pop(), 1);
    CHECK_EQ(s.size(), 0u);
    CHECK(throwsOutOfRange([&] { s.pop(); }));
}

static void testClearAndReuse() {
    Stack<int> s;
    for (int i = 0; i < 10; ++i) {
        s.push(i);
    }
    s.clear();
    CHECK(s.empty());
    CHECK_EQ(s.size(), 0u);
    s.push(42);
    CHECK_EQ(s.top(), 42);
    CHECK_EQ(s.size(), 1u);
}

// A copy must keep the same order and share no nodes with the original.
static void testCopyIsDeepAndOrdered() {
    Stack<std::string> a;
    a.push("bottom");
    a.push("middle");
    a.push("top");

    Stack<std::string> b(a);
    CHECK_EQ(b.size(), 3u);

    a.pop();
    a.push("changed");

    CHECK_EQ(b.pop(), std::string("top"));
    CHECK_EQ(b.pop(), std::string("middle"));
    CHECK_EQ(b.pop(), std::string("bottom"));
    CHECK_EQ(a.top(), std::string("changed"));
    CHECK_EQ(a.size(), 3u);
}

static void testCopyAssignment() {
    Stack<int> a;
    a.push(1);
    a.push(2);
    Stack<int> b;
    b.push(99);

    b = a;
    CHECK_EQ(b.size(), 2u);
    CHECK_EQ(b.pop(), 2);
    CHECK_EQ(a.size(), 2u);

    Stack<int>& alias = a;
    a = alias;
    CHECK_EQ(a.size(), 2u);
    CHECK_EQ(a.top(), 2);
}

static void testMoveLeavesSourceEmpty() {
    Stack<int> a;
    a.push(7);
    a.push(8);

    Stack<int> b(std::move(a));
    CHECK_EQ(b.size(), 2u);
    CHECK_EQ(b.top(), 8);
    CHECK(a.empty());

    Stack<int> c;
    c = std::move(b);
    CHECK_EQ(c.pop(), 8);
    CHECK_EQ(c.pop(), 7);
    CHECK(b.empty());
}

// A recursive destructor would crash here.
static void testLongStack() {
    const int n = 200000;
    Stack<int> s;
    for (int i = 0; i < n; ++i) {
        s.push(i);
    }
    CHECK_EQ(s.size(), static_cast<std::size_t>(n));
    CHECK_EQ(s.top(), n - 1);

    bool ordered = true;
    for (int want = n - 1; want >= n / 2; --want) {
        if (s.pop() != want) ordered = false;
    }
    CHECK(ordered);
    CHECK_EQ(s.size(), static_cast<std::size_t>(n / 2));
}

// The shape undo will use: a record per action, newest on top.
struct Action {
    std::string kind;
    int emergencyId;
    int ambulanceId;
    int hospitalId;
};

static void testUndoRedoPattern() {
    Stack<Action> undo;
    Stack<Action> redo;

    undo.push({"dispatch", 1, 4, 2});
    undo.push({"dispatch", 2, 5, 3});

    redo.push(undo.pop());
    CHECK_EQ(redo.top().emergencyId, 2);
    CHECK_EQ(undo.top().emergencyId, 1);

    undo.push(redo.pop());
    CHECK_EQ(undo.top().ambulanceId, 5);
    CHECK(redo.empty());

    redo.push(undo.pop());
    undo.push({"dispatch", 3, 6, 1});
    redo.clear();
    CHECK(redo.empty());
    CHECK_EQ(undo.size(), 2u);
    CHECK_EQ(undo.top().kind, std::string("dispatch"));
    CHECK_EQ(undo.top().emergencyId, 3);
}

int main() {
    testEmptyStack();
    testLastInFirstOut();
    testTopDoesNotRemove();
    testSizeThroughInterleavedOps();
    testClearAndReuse();
    testCopyIsDeepAndOrdered();
    testCopyAssignment();
    testMoveLeavesSourceEmpty();
    testLongStack();
    testUndoRedoPattern();
    return report("test_stack");
}
