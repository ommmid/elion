#include "elion_planner/planner.h"

#include <iostream>

#include "elion_planner/collision_checking.h"

namespace elion
{
bool isValid(const ob::State* state)
{
  return true;
}

ElionPlanner::ElionPlanner()
{
  ompl::msg::setLogLevel(ompl::msg::LOG_DEV2);  // TODO nut sure if this is working
}

void ElionPlanner::preSolve(robot_model::RobotModelConstPtr robot_model, const std::string& group,
                            planning_interface::MotionPlanRequest request)
{
  num_dofs_ = robot_model->getJointModelGroup(group)->getVariableCount();

  state_space_ = std::make_shared<ob::RealVectorStateSpace>(num_dofs_);
  ob::RealVectorBounds bounds(num_dofs_);
  bounds.setLow(-2 * M_PI);
  bounds.setHigh(2 * M_PI);
  state_space_->setBounds(bounds);

  constraints_ = createConstraint(robot_model, group, request.path_constraints);
  if (!constraints_)
  {
    ROS_ERROR_STREAM("Failed to create constraints");
    return;
  }

  constrained_state_space_ = std::make_shared<ob::ProjectedStateSpace>(state_space_, constraints_);
  constrained_state_space_info_ = std::make_shared<ob::ConstrainedSpaceInformation>(constrained_state_space_);

  simple_setup_ = std::make_shared<og::SimpleSetup>(constrained_state_space_info_);

  planner_ = std::make_shared<og::RRTConnect>(constrained_state_space_info_);

  simple_setup_->setPlanner(planner_);

  simple_setup_->setStateValidityChecker(std::make_shared<MoveItStateValidityChecker>(constrained_state_space_info_));
}

bool ElionPlanner::solve(const Eigen::Ref<const Eigen::VectorXd>& start_joint_positions,
                         const Eigen::Ref<const Eigen::VectorXd>& goal_joint_positions, double allowed_planning_time)
{
  ob::ScopedState<> start(constrained_state_space_);
  ob::ScopedState<> goal(constrained_state_space_);

  start->as<ob::ConstrainedStateSpace::StateType>()->copy(start_joint_positions);
  goal->as<ob::ConstrainedStateSpace::StateType>()->copy(goal_joint_positions);
  simple_setup_->setStartAndGoalStates(start, goal);

  // solving it
  simple_setup_->setup();
  ob::PlannerStatus stat = simple_setup_->solve(allowed_planning_time);
  if (stat)
  {
    return true;
  }
  else
  {
    OMPL_WARN("No solution found!");
    return false;
  }
}

void ElionPlanner::postSolve()
{
  simple_setup_->simplifySolution(5.);
  auto path = simple_setup_->getSolutionPath();
  path.interpolate();

  // path.printAsMatrix(std::cout);

  std::cout << "Writing path from OMPL to generic format." << std::endl;

  // write path to generic format indepenent from OMPL to pass it to ROS?
  solution_path_.clear();
  for (auto& state : path.getStates())
  {
    const Eigen::Map<Eigen::VectorXd>& x = *state->as<ob::ConstrainedStateSpace::StateType>();

    Eigen::VectorXd joint_position(x);
    solution_path_.push_back(joint_position);
  }
}
}  // namespace elion