#include "cppcoder/TaskQueue.h"

#include <gtest/gtest.h>

using cppcoder::Task;
using cppcoder::TaskQueue;

TEST(TaskQueueTest, PushAndPopPreservesOrder) {
    TaskQueue q;
    Task a{"a", "areaA", "goal", "criteria"};
    Task b{"b", "areaB", "goal", "criteria"};
    EXPECT_TRUE(q.Push(a));
    EXPECT_TRUE(q.Push(b));
    EXPECT_EQ(q.Size(), 2u);

    Task popped = q.Pop();
    EXPECT_EQ(popped.targetArea, "areaA");
    EXPECT_EQ(q.Size(), 1u);
}

TEST(TaskQueueTest, RejectsDuplicateQueuedArea) {
    TaskQueue q;
    Task a{"a", "areaA", "goal1", "criteria1"};
    Task dup{"dup", "areaA", "goal2", "criteria2"};
    EXPECT_TRUE(q.Push(a));
    EXPECT_FALSE(q.Push(dup));
    EXPECT_EQ(q.Size(), 1u);
}

TEST(TaskQueueTest, RejectsVisitedArea) {
    TaskQueue q;
    q.MarkVisited("areaA");
    Task a{"a", "areaA", "goal", "criteria"};
    EXPECT_FALSE(q.Push(a));
    EXPECT_TRUE(q.Empty());
}

TEST(TaskQueueTest, AllowsRequeueAfterPop) {
    TaskQueue q;
    Task a{"a", "areaA", "goal", "criteria"};
    EXPECT_TRUE(q.Push(a));
    q.Pop();
    // Not marked visited yet -- area should be re-queueable.
    EXPECT_TRUE(q.Push(a));
}

TEST(TaskQueueTest, EmptyQueueReportsEmptyAndZeroSize) {
    TaskQueue q;
    EXPECT_TRUE(q.Empty());
    EXPECT_EQ(q.Size(), 0u);
}

TEST(TaskQueueTest, PopDecreasesSizeAndClearsEmptyOnLastItem) {
    TaskQueue q;
    q.Push(Task{"a", "areaA", "g", "c"});
    EXPECT_FALSE(q.Empty());
    q.Pop();
    EXPECT_TRUE(q.Empty());
}

TEST(TaskQueueTest, FifoOrderHoldsForThreePlusItems) {
    TaskQueue q;
    q.Push(Task{"a", "1", "g", "c"});
    q.Push(Task{"b", "2", "g", "c"});
    q.Push(Task{"c", "3", "g", "c"});
    EXPECT_EQ(q.Pop().targetArea, "1");
    EXPECT_EQ(q.Pop().targetArea, "2");
    EXPECT_EQ(q.Pop().targetArea, "3");
    EXPECT_TRUE(q.Empty());
}

TEST(TaskQueueTest, VisitedQueryReflectsMarkVisited) {
    TaskQueue q;
    EXPECT_FALSE(q.Visited("areaA"));
    q.MarkVisited("areaA");
    EXPECT_TRUE(q.Visited("areaA"));
    EXPECT_FALSE(q.Visited("areaB"));
}

TEST(TaskQueueTest, MarkVisitedDoesNotAffectQueuedButUnvisitedArea) {
    TaskQueue q;
    q.Push(Task{"a", "areaA", "g", "c"});
    // Area is queued but not yet visited -- Visited() should say so.
    EXPECT_FALSE(q.Visited("areaA"));
}

TEST(TaskQueueTest, RepeatableTaskDedupesOnUmbrellaArea) {
    TaskQueue q;
    Task rep;
    rep.id = "seed";
    rep.targetArea = "(probe: 3 files)";
    rep.repeatable = true;
    rep.repeatTargets = {"a.cpp", "b.cpp", "c.cpp"};
    EXPECT_TRUE(q.Push(rep));

    Task repDup = rep;
    repDup.id = "seed2";
    EXPECT_FALSE(q.Push(repDup));
    EXPECT_EQ(q.Size(), 1u);
}

TEST(TaskQueueTest, RepeatableTaskRejectedIfUmbrellaAreaVisited) {
    TaskQueue q;
    q.MarkVisited("(probe: 3 files)");
    Task rep;
    rep.id = "seed";
    rep.targetArea = "(probe: 3 files)";
    rep.repeatable = true;
    rep.repeatTargets = {"a.cpp"};
    EXPECT_FALSE(q.Push(rep));
}

TEST(TaskQueueTest, DifferentAreasCanCoexist) {
    TaskQueue q;
    EXPECT_TRUE(q.Push(Task{"a", "areaA", "g", "c"}));
    EXPECT_TRUE(q.Push(Task{"b", "areaB", "g", "c"}));
    EXPECT_TRUE(q.Push(Task{"c", "areaC", "g", "c"}));
    EXPECT_EQ(q.Size(), 3u);
}

TEST(TaskQueueTest, PoppedTaskRetainsAllOriginalFields) {
    TaskQueue q;
    Task t{"id-1", "areaX", "find the thing", "thing is found"};
    t.depth = 3;
    t.parentId = "parent-1";
    q.Push(t);
    Task popped = q.Pop();
    EXPECT_EQ(popped.id, "id-1");
    EXPECT_EQ(popped.researchGoal, "find the thing");
    EXPECT_EQ(popped.successCriteria, "thing is found");
    EXPECT_EQ(popped.depth, 3);
    EXPECT_EQ(popped.parentId, "parent-1");
}

TEST(TaskQueueTest, RejectsDuplicateArea_PreservesOriginalTaskFields) {
    TaskQueue q;
    Task original{"orig", "shared-area", "original goal", "original criteria"};
    Task duplicate{"dup", "shared-area", "different goal", "different criteria"};
    EXPECT_TRUE(q.Push(original));
    EXPECT_FALSE(q.Push(duplicate));
    EXPECT_EQ(q.Size(), 1u);

    Task popped = q.Pop();
    EXPECT_EQ(popped.id, "orig");
    EXPECT_EQ(popped.researchGoal, "original goal");
    EXPECT_EQ(popped.successCriteria, "original criteria");
}

TEST(TaskQueueTest, PushRejectedAfterAreaVisitedPostPop) {
    TaskQueue q;
    Task first{"a", "areaZ", "g", "c"};
    EXPECT_TRUE(q.Push(first));
    q.Pop();  // frees areaZ from queuedAreas_, but not visited_

    q.MarkVisited("areaZ");
    Task second{"b", "areaZ", "g2", "c2"};
    EXPECT_FALSE(q.Push(second));
    EXPECT_TRUE(q.Empty());
}

TEST(TaskQueueTest, TrailingSlashIsDistinctArea) {
    TaskQueue q;
    EXPECT_TRUE(q.Push(Task{"a", "src/foo", "g", "c"}));
    EXPECT_TRUE(q.Push(Task{"b", "src/foo/", "g", "c"}));
    EXPECT_EQ(q.Size(), 2u);
}

TEST(TaskQueueTest, AreaComparisonIsCaseSensitive) {
    TaskQueue q;
    EXPECT_TRUE(q.Push(Task{"a", "AreaCase", "g", "c"}));
    EXPECT_TRUE(q.Push(Task{"b", "areacase", "g", "c"}));
    EXPECT_EQ(q.Size(), 2u);
}

TEST(TaskQueueTest, InterleavedPushPopMarkVisitedMaintainsFifoAcrossManyTasks) {
    TaskQueue q;
    for (int i = 0; i < 10; ++i) {
        std::string area = "a" + std::to_string(i);
        EXPECT_TRUE(q.Push(Task{area, area, "g", "c"}));
    }
    EXPECT_EQ(q.Size(), 10u);

    EXPECT_EQ(q.Pop().targetArea, "a0");
    EXPECT_EQ(q.Pop().targetArea, "a1");
    EXPECT_EQ(q.Pop().targetArea, "a2");

    q.MarkVisited("a0");  // already popped -- should not disturb remaining order
    EXPECT_TRUE(q.Push(Task{"a10", "a10", "g", "c"}));

    for (int i = 3; i <= 10; ++i) {
        std::string expected = "a" + std::to_string(i);
        EXPECT_EQ(q.Pop().targetArea, expected);
    }
    EXPECT_TRUE(q.Empty());
}

TEST(TaskQueueTest, PopDoesNotImplicitlyMarkAreaVisited) {
    TaskQueue q;
    q.Push(Task{"a", "areaW", "g", "c"});
    EXPECT_FALSE(q.Visited("areaW"));
    q.Pop();
    // Popping a task must not implicitly mark its area visited.
    EXPECT_FALSE(q.Visited("areaW"));
}

TEST(TaskQueueTest, VisitedTrueImmediatelyAfterMarkVisitedForNeverQueuedArea) {
    TaskQueue q;
    EXPECT_TRUE(q.Empty());
    q.MarkVisited("never-pushed-area");
    EXPECT_TRUE(q.Visited("never-pushed-area"));
    EXPECT_TRUE(q.Empty());
    EXPECT_EQ(q.Size(), 0u);
}

TEST(TaskQueueTest, SizeAndEmptyConsistencyThroughPushPopSequence) {
    TaskQueue q;
    EXPECT_TRUE(q.Empty());
    q.Push(Task{"a", "area1", "g", "c"});
    q.Push(Task{"b", "area2", "g", "c"});
    q.Push(Task{"c", "area3", "g", "c"});
    EXPECT_EQ(q.Size(), 3u);
    EXPECT_FALSE(q.Empty());

    q.Pop();
    EXPECT_EQ(q.Size(), 2u);
    EXPECT_FALSE(q.Empty());

    q.Pop();
    EXPECT_EQ(q.Size(), 1u);
    EXPECT_FALSE(q.Empty());

    q.Pop();
    EXPECT_EQ(q.Size(), 0u);
    EXPECT_TRUE(q.Empty());
}

TEST(TaskQueueTest, PushEmptyStringTargetArea) {
    TaskQueue q;
    Task t{"a", "", "goal", "criteria"};
    EXPECT_TRUE(q.Push(t));
    EXPECT_EQ(q.Size(), 1u);

    Task popped = q.Pop();
    EXPECT_EQ(popped.targetArea, "");
    EXPECT_TRUE(q.Empty());

    // Not visited/marked, so the empty area can be re-queued.
    EXPECT_TRUE(q.Push(Task{"b", "", "g2", "c2"}));
    q.Pop();

    q.MarkVisited("");
    EXPECT_FALSE(q.Push(Task{"c", "", "g3", "c3"}));
}

TEST(TaskQueueTest, PushManyDistinctAreasPopOrderMatchesPushOrder) {
    TaskQueue q;
    const std::vector<std::string> ids = {"t0", "t1", "t2", "t3", "t4", "t5"};
    for (const auto& id : ids) {
        EXPECT_TRUE(q.Push(Task{id, "area-" + id, "g", "c"}));
    }
    for (const auto& id : ids) {
        Task popped = q.Pop();
        EXPECT_EQ(popped.id, id);
        EXPECT_EQ(popped.targetArea, "area-" + id);
    }
    EXPECT_TRUE(q.Empty());
}

TEST(TaskQueueTest, MarkVisitedTwiceSameAreaIsIdempotent) {
    TaskQueue q;
    q.MarkVisited("dup-area");
    q.MarkVisited("dup-area");
    EXPECT_TRUE(q.Visited("dup-area"));
    EXPECT_FALSE(q.Push(Task{"a", "dup-area", "g", "c"}));
    EXPECT_TRUE(q.Empty());
}

TEST(TaskQueueTest, RepeatableTaskPreservesRepeatTargetsThroughPushPop) {
    TaskQueue q;
    Task rep;
    rep.id = "seed";
    rep.targetArea = "(probe: 3 files)";
    rep.repeatable = true;
    rep.repeatTargets = {"x.cpp", "y.cpp", "z.cpp"};
    EXPECT_TRUE(q.Push(rep));

    Task popped = q.Pop();
    EXPECT_TRUE(popped.repeatable);
    ASSERT_EQ(popped.repeatTargets.size(), 3u);
    EXPECT_EQ(popped.repeatTargets[0], "x.cpp");
    EXPECT_EQ(popped.repeatTargets[1], "y.cpp");
    EXPECT_EQ(popped.repeatTargets[2], "z.cpp");
}

TEST(TaskQueueTest, PushSucceedsAfterQueueFullyDrainedEmpty) {
    TaskQueue q;
    q.Push(Task{"a", "areaA", "g", "c"});
    q.Push(Task{"b", "areaB", "g", "c"});
    q.Pop();
    q.Pop();
    ASSERT_TRUE(q.Empty());

    EXPECT_TRUE(q.Push(Task{"c", "areaC", "g", "c"}));
    EXPECT_EQ(q.Size(), 1u);
    EXPECT_FALSE(q.Empty());
}

TEST(TaskQueueTest, MarkVisitedThenPushDifferentTaskSameAreaRejected) {
    TaskQueue q;
    q.MarkVisited("shared-area");

    Task differentTask{"totally-different-id", "shared-area", "different goal", "different criteria"};
    differentTask.depth = 7;
    differentTask.parentId = "some-parent";
    EXPECT_FALSE(q.Push(differentTask));
    EXPECT_TRUE(q.Empty());
    EXPECT_EQ(q.Size(), 0u);
}

TEST(TaskQueueTest, RequeueAfterPopAllowsDifferentTaskDataSameArea) {
    TaskQueue q;
    Task first{"first-id", "areaQ", "goal-1", "criteria-1"};
    EXPECT_TRUE(q.Push(first));
    q.Pop();

    // Same area, but a brand-new task object (not marked visited) -- must
    // be allowed to re-queue with entirely different field values.
    Task second{"second-id", "areaQ", "goal-2", "criteria-2"};
    second.depth = 5;
    EXPECT_TRUE(q.Push(second));
    Task popped = q.Pop();
    EXPECT_EQ(popped.id, "second-id");
    EXPECT_EQ(popped.researchGoal, "goal-2");
    EXPECT_EQ(popped.depth, 5);
}

TEST(TaskQueueTest, MultipleRepeatableTasksWithDifferentAreasCoexist) {
    TaskQueue q;
    Task rep1;
    rep1.id = "rep1";
    rep1.targetArea = "(probe: group1)";
    rep1.repeatable = true;
    rep1.repeatTargets = {"a.cpp"};

    Task rep2;
    rep2.id = "rep2";
    rep2.targetArea = "(probe: group2)";
    rep2.repeatable = true;
    rep2.repeatTargets = {"b.cpp", "c.cpp"};

    EXPECT_TRUE(q.Push(rep1));
    EXPECT_TRUE(q.Push(rep2));
    EXPECT_EQ(q.Size(), 2u);

    Task poppedFirst = q.Pop();
    EXPECT_EQ(poppedFirst.id, "rep1");
    Task poppedSecond = q.Pop();
    EXPECT_EQ(poppedSecond.id, "rep2");
    ASSERT_EQ(poppedSecond.repeatTargets.size(), 2u);
}
