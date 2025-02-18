/*
 * Copyright 2012-2019 CNRS-UM LIRMM, CNRS-AIST JRL
 */

// associated header
#include "Tasks/QPMotionConstr.h"

// includes
// Eigen
#include <ostream>
#include <string>
#include <unsupported/Eigen/Polynomials>

// RBDyn
#include <RBDyn/MultiBody.h>
#include <RBDyn/MultiBodyConfig.h>

// Tasks
#include "Tasks/Bounds.h"
#include "utils.h"

#include <fstream>
#include <iomanip> // For std::setw
#include <iostream>

namespace tasks
{

namespace qp
{

/**
 *															PositiveLambda
 */

PositiveLambda::PositiveLambda() : lambdaBegin_(-1), XL_(), XU_(), cont_() {}

void PositiveLambda::updateNrVars(const std::vector<rbd::MultiBody> & /* mbs */, const SolverData & data)
{
  lambdaBegin_ = data.lambdaBegin();

  XL_.setConstant(data.totalLambda(), 0.);
  XU_.setConstant(data.totalLambda(), std::numeric_limits<double>::infinity());

  cont_.clear();
  const std::vector<BilateralContact> & allC = data.allContacts();
  for(std::size_t i = 0; i < allC.size(); ++i)
  {
    cont_.push_back({allC[i].contactId, data.lambdaBegin(int(i)), allC[i].nrLambda()});
  }
}

void PositiveLambda::update(const std::vector<rbd::MultiBody> & /* mbs */,
                            const std::vector<rbd::MultiBodyConfig> & /* mbc */,
                            const SolverData & /* data */)
{
}

std::string PositiveLambda::nameBound() const
{
  return "PositiveLambda";
}

std::string PositiveLambda::descBound(const std::vector<rbd::MultiBody> & /* mbs */, int line)
{
  std::ostringstream oss;

  for(const ContactData & cd : cont_)
  {
    int begin = cd.lambdaBegin - lambdaBegin_;
    int end = begin + cd.nrLambda;
    if(line >= begin && line < end)
    {
      oss << "Body 1: " << cd.cId.r1BodyName << std::endl;
      oss << "Body 2: " << cd.cId.r2BodyName << std::endl;
      break;
    }
  }

  return oss.str();
}

int PositiveLambda::beginVar() const
{
  return lambdaBegin_;
}

const Eigen::VectorXd & PositiveLambda::Lower() const
{
  return XL_;
}

const Eigen::VectorXd & PositiveLambda::Upper() const
{
  return XU_;
}

/**
 *															MotionConstrCommon
 */

MotionConstrCommon::ContactData::ContactData(const rbd::MultiBody & mb,
                                             const std::string & bName,
                                             int lB,
                                             std::vector<Eigen::Vector3d> pts,
                                             const std::vector<FrictionCone> & cones)
: bodyIndex(), lambdaBegin(lB), jac(mb, bName), points(std::move(pts)), minusGenerators(cones.size())
{
  bodyIndex = jac.jointsPath().back();
  for(std::size_t i = 0; i < cones.size(); ++i)
  {
    minusGenerators[i].resize(3, cones[i].generators.size());
    for(std::size_t j = 0; j < cones[i].generators.size(); ++j) { minusGenerators[i].col(j) = -cones[i].generators[j]; }
  }
}

MotionConstrCommon::MotionConstrCommon(const std::vector<rbd::MultiBody> & mbs, int robotIndex)
: robotIndex_(robotIndex), alphaDBegin_(-1), nrDof_(mbs[robotIndex_].nrDof()), lambdaBegin_(-1), fd_(mbs[robotIndex_]),
  fullJacLambda_(), jacTrans_(6, nrDof_), jacLambda_(), cont_(), curTorque_(nrDof_), A_(), AL_(nrDof_), AU_(nrDof_)
{
  assert(std::size_t(robotIndex_) < mbs.size() && robotIndex_ >= 0);
  // This is technically incorrect but practically not a huge deal, see #66
  curTorque_.setZero();
}

void MotionConstrCommon::computeTorque(const Eigen::VectorXd & alphaD, const Eigen::VectorXd & lambda)
{
  curTorque_ = fd_.H() * alphaD.segment(alphaDBegin_, nrDof_);
  curTorque_ += fd_.C();
  curTorque_ += A_.block(0, lambdaBegin_, nrDof_, A_.cols() - lambdaBegin_) * lambda;

  for(std::size_t i = 0; i < cont_.size(); ++i)
  {
    ContactData & cd = cont_[i];
    for(std::size_t j = 0; j < cd.points.size(); ++j)
    {
      std::ofstream file("force" + std::to_string(j) + std::to_string(i) + ".csv");
      std::ofstream coeff("coeff" + std::to_string(j) + std::to_string(i) + ".csv");

      if(file.is_open() and coeff.is_open())
      {
        std::cout << " reappered" << std::endl;
        std::cout << "segment" << i << j << ": " << std::endl;
        std::cout << lambda.segment(4 * j + 16 * i, 4) << std::endl;
        std::cout << "----" << std::endl;

        auto & matrix = cd.minusGenerators[j];
        auto & lambda_segment = lambda.segment(4 * j + 16 * i, 4);

        file << cd.minusGenerators[j] * lambda.segment(4 * j + 16 * i, 4);
        coeff << cd.minusGenerators[j];
        std::cout << "Success force" << i << j << std::endl;
        std::cout << "Success coeff" << i << j << std::endl;
        file.close();
        coeff.close();
      }
      else
      {
        std::cout << "Error force" << i << j << std::endl;
        std::cout << "Error coeff" << i << j << std::endl;
      }
    }
  }

  std::ofstream file0("fd_H.csv");

  if(file0.is_open())
  {
    file0 << fd_.H();
    file0.close();
    std::cout << "success fd_H" << std::endl;
  }
  else { std::cout << "error with fd_H" << std::endl; }

  std::ofstream file1("alphaDsegment.csv");

  if(file1.is_open())
  {
    file1 << alphaD.segment(alphaDBegin_, nrDof_);
    file1.close();
    std::cout << "success alphaD" << std::endl;
  }
  else { std::cout << "error with alphaDsegment" << std::endl; }

  std::ofstream file2("fd_C.csv");

  if(file2.is_open())
  {
    file2 << fd_.C();
    file2.close();
    std::cout << "success fd_C" << std::endl;
  }
  else { std::cout << "error with fd_C" << std::endl; }

  std::ofstream file3("A.csv");

  if(file3.is_open())
  {
    file3 << A_.block(0, lambdaBegin_, nrDof_, A_.cols() - lambdaBegin_);
    file3.close();
    std::cout << "success A" << std::endl;
  }
  else { std::cout << "error with A" << std::endl; }

  std::cout << "size lambda: " << lambda.size() / 8 << std::endl;
  for(int k = 0; k < lambda.size() / 8; k++)
  {
    std::ofstream file4("lambda" + std::to_string(k) + ".csv");
    if(file4.is_open())
    {
      file4 << lambda.segment(4 * k, 4);
      file4.close();
      std::cout << "success lambda" << k << std::endl;
    }
    else { std::cout << "error with lambda" << k << std::endl; }
  }

  std::ofstream file5("lambda.csv");

  if(file5.is_open())
  {
    file5 << lambda;
    std::cout << "success lambda" << std::endl;
  }
  else { std::cout << "error with lambda" << std::endl; }

  std::ofstream file6("curTorque.csv");

  if(file6.is_open())
  {
    file6 << curTorque_;
    std::cout << "success current torque" << std::endl;
  }
  else { std::cout << "error with current torque" << std::endl; }
}

const Eigen::VectorXd & MotionConstrCommon::torque() const
{
  return curTorque_;
}

void MotionConstrCommon::torque(const std::vector<rbd::MultiBody> & mbs, std::vector<rbd::MultiBodyConfig> & mbcs) const
{
  const rbd::MultiBody & mb = mbs[robotIndex_];
  rbd::MultiBodyConfig & mbc = mbcs[robotIndex_];

  int pos = mb.joint(0).dof();
  for(std::size_t i = 1; i < mbc.jointTorque.size(); ++i)
  {
    for(double & d : mbc.jointTorque[i])
    {
      d = curTorque_(pos);
      ++pos;
    }
  }
}

void MotionConstrCommon::updateNrVars(const std::vector<rbd::MultiBody> & mbs, const SolverData & data)
{
  const rbd::MultiBody & mb = mbs[robotIndex_];

  alphaDBegin_ = data.alphaDBegin(robotIndex_);
  lambdaBegin_ = data.lambdaBegin();

  cont_.clear();
  const auto & cCont = data.allContacts();

  for(std::size_t i = 0; i < cCont.size(); ++i)
  {
    const BilateralContact & c = cCont[i];
    if(robotIndex_ == c.contactId.r1Index)
    {
      cont_.emplace_back(mb, c.contactId.r1BodyName, data.lambdaBegin(int(i)), c.r1Points, c.r1Cones);
    }
    // we don't use else to manage self contact on the robot
    if(robotIndex_ == c.contactId.r2Index)
    {
      cont_.emplace_back(mb, c.contactId.r2BodyName, data.lambdaBegin(int(i)), c.r2Points, c.r2Cones);
    }
  }

  /// @todo don't use nrDof and totalLamdba but max dof of a jacobian
  /// and max lambda of a contact.
  A_.setZero(nrDof_, data.nrVars());
  jacLambda_.resize(data.totalLambda(), nrDof_);
  fullJacLambda_.resize(data.totalLambda(), nrDof_);
}

void MotionConstrCommon::computeMatrix(const std::vector<rbd::MultiBody> & mbs,
                                       const std::vector<rbd::MultiBodyConfig> & mbcs)
{
  using namespace Eigen;

  const rbd::MultiBody & mb = mbs[robotIndex_];
  const rbd::MultiBodyConfig & mbc = mbcs[robotIndex_];

  fd_.computeH(mb, mbc);
  fd_.computeC(mb, mbc);

  // tauMin -C <= H*alphaD - J^t G lambda <= tauMax - C

  // fill inertia matrix part
  A_.block(0, alphaDBegin_, nrDof_, nrDof_) = fd_.H();

  std::cout << "cont_.size(): " << cont_.size() << std::endl;

  for(std::size_t i = 0; i < cont_.size(); ++i)
  {
    const MatrixXd & jac = cont_[i].jac.bodyJacobian(mb, mbc);

    ContactData & cd = cont_[i];
    int lambdaOffset = 0;

    std::cout << "cd.points.size(): " << cd.points.size() << std::endl;

    for(std::size_t j = 0; j < cd.points.size(); ++j)
    {
      int nrLambda = int(cd.minusGenerators[j].cols());
      // we translate the jacobian to the contact point
      // then we compute the jacobian against lambda J_l = J^T C
      // to apply fullJacobian on it we must have robot dof on the column so
      // J_l^T = (J^T C)^T = C^T J
      cd.jac.translateBodyJacobian(jac, mbc, cd.points[j], jacTrans_);
      jacLambda_.block(0, 0, nrLambda, cd.jac.dof()).noalias() =
          (cd.minusGenerators[j].transpose() * jacTrans_.block(3, 0, 3, cd.jac.dof()));

      cd.jac.fullJacobian(mb, jacLambda_.block(0, 0, nrLambda, cd.jac.dof()), fullJacLambda_);

      // A_.block(0, lambdaBegin_, nrDof_, A_.cols() - lambdaBegin_)

      std::ofstream file("jac_" + std::to_string(j) + std::to_string(i) + ".csv");
      if(file.is_open())
      {
        file << jacTrans_.block(3, 0, 3, cd.jac.dof());
        file.close();
        std ::cout << "success jac_" + std::to_string(j) + std::to_string(i) + ".csv" << std::endl;
      }
      else { std::cout << "error with jac_" + std::to_string(j) + std::to_string(i) + ".csv" << std::endl; }

      A_.block(0, cd.lambdaBegin + lambdaOffset, nrDof_, nrLambda).noalias() =
          fullJacLambda_.block(0, 0, nrLambda, nrDof_).transpose();
      lambdaOffset += nrLambda;
    }
  }

  // BEq = -C
  AL_ = -fd_.C();
  AU_ = -fd_.C();
}

int MotionConstrCommon::maxGenInEq() const
{
  return int(A_.rows());
}

const Eigen::MatrixXd & MotionConstrCommon::AGenInEq() const
{
  return A_;
}

const Eigen::VectorXd & MotionConstrCommon::LowerGenInEq() const
{
  return AL_;
}

const Eigen::VectorXd & MotionConstrCommon::UpperGenInEq() const
{
  return AU_;
}

std::string MotionConstrCommon::nameGenInEq() const
{
  return "MotionConstr";
}

std::string MotionConstrCommon::descGenInEq(const std::vector<rbd::MultiBody> & mbs, int line)
{
  int jIndex = findJointFromVector(mbs[robotIndex_], line, true);
  return std::string("Joint: ") + mbs[robotIndex_].joint(jIndex).name();
}

/**
 *															MotionConstr
 */

MotionConstr::MotionConstr(const std::vector<rbd::MultiBody> & mbs, int robotIndex, const TorqueBound & tb)
: MotionConstr(mbs, robotIndex, tb, {}, 0.001)
{
}

MotionConstr::MotionConstr(const std::vector<rbd::MultiBody> & mbs,
                           int robotIndex,
                           const TorqueBound & tb,
                           const TorqueDBound & tdb,
                           double dt)
: MotionConstrCommon(mbs, robotIndex), torqueL_(mbs[robotIndex].nrDof()), torqueU_(mbs[robotIndex].nrDof()),
  torqueDtL_(mbs[robotIndex].nrDof()), torqueDtU_(mbs[robotIndex].nrDof()), tmpL_(nrDof_), tmpU_(nrDof_)
{
  rbd::paramToVector(tb.lTorqueBound, torqueL_);
  rbd::paramToVector(tb.uTorqueBound, torqueU_);
  torqueDtL_.setConstant(-std::numeric_limits<double>::infinity());
  torqueDtU_.setConstant(std::numeric_limits<double>::infinity());
  rbd::paramToVector(tdb.lTorqueDBound, torqueDtL_);
  rbd::paramToVector(tdb.uTorqueDBound, torqueDtU_);
  torqueDtL_ *= dt;
  torqueDtU_ *= dt;
}

void MotionConstr::update(const std::vector<rbd::MultiBody> & mbs,
                          const std::vector<rbd::MultiBodyConfig> & mbcs,
                          const SolverData & /* data */)
{
  computeMatrix(mbs, mbcs);

  // max[tauMin, tauDMin*dt + tau(k-1)] - C <= H*alphaD - J^t G lambda <= min[tauMax, tauDMax * dt + tau(k-1)] - C
  if(updateIter_++ > 0)
  {
    AL_.head(torqueL_.rows()) += torqueL_.cwiseMax(torqueDtL_ + curTorque_);
    AU_.head(torqueL_.rows()) += torqueU_.cwiseMin(torqueDtU_ + curTorque_);
  }
  else
  {
    AL_.head(torqueL_.rows()) += torqueL_;
    AU_.head(torqueU_.rows()) += torqueU_;
  }
}

Eigen::MatrixXd MotionConstr::contactMatrix() const
{
  return A_.block(0, nrDof_, A_.rows(), A_.cols() - nrDof_);
}

const rbd::ForwardDynamics MotionConstr::fd() const
{
  return fd_;
}

/**
 *															MotionSpringConstr
 */

MotionSpringConstr::MotionSpringConstr(const std::vector<rbd::MultiBody> & mbs,
                                       int robotIndex,
                                       const TorqueBound & tb,
                                       const std::vector<SpringJoint> & springs)
: MotionSpringConstr(mbs, robotIndex, tb, {}, 0.001, springs)
{
}

MotionSpringConstr::MotionSpringConstr(const std::vector<rbd::MultiBody> & mbs,
                                       int robotIndex,
                                       const TorqueBound & tb,
                                       const TorqueDBound & tdb,
                                       double dt,
                                       const std::vector<SpringJoint> & springs)
: MotionConstr(mbs, robotIndex, tb, tdb, dt), springs_()
{
  const rbd::MultiBody & mb = mbs[robotIndex_];
  for(const SpringJoint & sj : springs)
  {
    int index = mb.jointIndexByName(sj.jointName);
    int posInDof = mb.jointPosInDof(index);
    springs_.push_back({index, posInDof, sj.K, sj.C, sj.O});
  }
}

void MotionSpringConstr::update(const std::vector<rbd::MultiBody> & mbs,
                                const std::vector<rbd::MultiBodyConfig> & mbcs,
                                const SolverData & /* data */)
{
  const rbd::MultiBodyConfig & mbc = mbcs[robotIndex_];

  computeMatrix(mbs, mbcs);

  for(const SpringJointData & sj : springs_)
  {
    double spring = mbc.q[sj.index][0] * sj.K + mbc.alpha[sj.index][0] * sj.C + sj.O;
    torqueL_(sj.posInDof) = -spring;
    torqueU_(sj.posInDof) = -spring;
  }

  if(updateIter_++ > 0)
  {
    AL_.head(torqueL_.rows()) += torqueL_.cwiseMax(torqueDtL_ + curTorque_);
    AU_.head(torqueL_.rows()) += torqueU_.cwiseMin(torqueDtU_ + curTorque_);
  }
  else
  {
    AL_.head(torqueL_.rows()) += torqueL_;
    AU_.head(torqueL_.rows()) += torqueU_;
  }
}

/**
 *															MotionPolyConstr
 */

MotionPolyConstr::MotionPolyConstr(const std::vector<rbd::MultiBody> & mbs, int robotIndex, const PolyTorqueBound & ptb)
: MotionConstrCommon(mbs, robotIndex), torqueL_(), torqueU_(), jointIndex_()
{
  const rbd::MultiBody & mb = mbs[robotIndex_];

  for(int i = 0; i < mb.nrJoints(); ++i)
  {
    if(mb.joint(i).dof() == 1)
    {
      torqueL_.push_back(ptb.lPolyTorqueBound[i][0]);
      torqueU_.push_back(ptb.uPolyTorqueBound[i][0]);
      jointIndex_.push_back(i);
    }
  }
}

void MotionPolyConstr::update(const std::vector<rbd::MultiBody> & mbs,
                              const std::vector<rbd::MultiBodyConfig> & mbcs,
                              const SolverData & /* data */)
{
  const rbd::MultiBody & mb = mbs[robotIndex_];
  const rbd::MultiBodyConfig & mbc = mbcs[robotIndex_];

  computeMatrix(mbs, mbcs);

  for(std::size_t i = 0; i < jointIndex_.size(); ++i)
  {
    int index = jointIndex_[i];
    int dofPos = mb.jointPosInDof(index);
    AL_(dofPos) += Eigen::poly_eval(torqueL_[i], mbc.q[index][0]);
    AU_(dofPos) += Eigen::poly_eval(torqueU_[i], mbc.q[index][0]);
  }
}

} // namespace qp

} // namespace tasks
