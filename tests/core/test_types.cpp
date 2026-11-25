#include "test_framework.hpp"
#include "luma/core/types.hpp"

#include <cstring>
#include <thread>

using namespace luma::core;

LUMA_TEST(T01_PodContractsAndSizes) {
    LUMA_EXPECT_EQ(sizeof(FrameRef), std::size_t{16});
    LUMA_EXPECT_EQ(sizeof(Point3f), std::size_t{16});
    LUMA_EXPECT_EQ(sizeof(ArmPose), std::size_t{24});
    LUMA_EXPECT_TRUE(sizeof(SkillCmdMsg) <= std::size_t{256});
    LUMA_EXPECT_TRUE(std::is_trivially_copyable_v<ArmStateMsg>);
    LUMA_EXPECT_TRUE(std::is_trivially_copyable_v<SceneStateMsg>);
    LUMA_EXPECT_TRUE(std::is_trivially_copyable_v<BookOffsetMsg>);
    LUMA_EXPECT_TRUE(std::is_trivially_copyable_v<HeadPoseMsg>);
    LUMA_EXPECT_TRUE(std::is_trivially_copyable_v<SkillCmdAckMsg>);
    LUMA_EXPECT_TRUE(std::is_trivially_copyable_v<SafetyEventMsg>);
    LUMA_EXPECT_TRUE(std::is_trivially_copyable_v<HeartbeatMsg>);
}

LUMA_TEST(T01_BackpressureTopicSemantics) {
    // 高频状态流：Drop-Oldest（丢旧保新）
    LUMA_EXPECT_TRUE(isDropOldestTopic(TopicId::kPerceptionSceneState));
    LUMA_EXPECT_TRUE(isDropOldestTopic(TopicId::kPerceptionBookOffset));
    LUMA_EXPECT_TRUE(isDropOldestTopic(TopicId::kPerceptionHeadPose));
    LUMA_EXPECT_TRUE(isDropOldestTopic(TopicId::kMotionArmState));
    LUMA_EXPECT_TRUE(isDropOldestTopic(TopicId::kSysHeartbeat));

    // 低频控制与安全事件：绝不静默丢弃（QueueFull）
    LUMA_EXPECT_FALSE(isDropOldestTopic(TopicId::kMotionSkillCmd));
    LUMA_EXPECT_FALSE(isDropOldestTopic(TopicId::kMotionCmdAck));
    LUMA_EXPECT_FALSE(isDropOldestTopic(TopicId::kSafetyEvent));
}

LUMA_TEST(T01_SteadyClockAndStatus) {
    const SteadyMs t1 = steadyNowMs();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    const SteadyMs t2 = steadyNowMs();
    LUMA_EXPECT_TRUE(t2 >= t1);

    const Status ok = Status::Ok();
    LUMA_EXPECT_TRUE(ok.ok());
    LUMA_EXPECT_EQ(std::strcmp(errorCodeName(ErrorCode::kStaleFrame), "kStaleFrame"), 0);
}

LUMA_TEST_MAIN()
