#!/bin/bash

# Handle commandline args
while [ "$1" != "" ]; do
    case $1 in
        -c )  # Commits since prevous tag
            TARGET="count" ;;
         * )
            TARGET="count"
            break ;;
    esac
    shift 1
done

TAG_PREFIX=$1
reg_ex="${TAG_PREFIX}*"

commits_since_last_tag() {
  GIT_BRANCH=(`git branch --show-current`)
  TAG_ARR=(`git tag --sort=committerdate --merged ${GIT_BRANCH} -l ${reg_ex} | tail -2`)
  PREVIOUS_TAG=${TAG_ARR[1]}
  CURRENT_TAG=${TAG_ARR[0]}
  NUM_COMMITS=0

  if [ "$CURRENT_TAG" != "" ];
  then
     CURR_CMT_NUM=`git rev-list --count $CURRENT_TAG`
  fi

  if [ "$PREVIOUS_TAG" != "" ];
  then
    PREV_CMT_NUM=`git rev-list --count $PREVIOUS_TAG`
    # Commits since prevous tag:
    let NUM_COMMITS="${PREV_CMT_NUM}-${CURR_CMT_NUM}"
  fi
  echo $NUM_COMMITS
}

case $TARGET in
    count) commits_since_last_tag ;;
    *) die "Invalid target $target" ;;
esac

exit 0

