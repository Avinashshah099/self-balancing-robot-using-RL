
#include <iostream>
#include <vector>

#include "gridWorld.hpp"

#include <rl/RL.hpp>
#include <rl/sarsa.hpp>

#define MAX_EPISODE 100

using namespace std;

int main()
{
    // create main variables
    signed short int time_step, reward;
    unsigned int wins, loses, wins_prev=0;
    float td_error, td_target, discount_factor, alpha, epsilon;
    char current_state, goal_state, action, next_state, current_state_idx, next_state_idx, max_action_idx;
    char next_action;
    std::vector<bool> available_actions(4, false);
    std::vector<float> q_row(4);

    // create object instances
    sarsa controller;
    grid_world env;

    srand(time(NULL));//seed the randomizer

    // rl variables (put in controller?)
    discount_factor = 0.3;
    alpha = 0.3;
    epsilon = 0.5;

    wins = 0;
    loses = 0;


    for (int episode = 0; episode < MAX_EPISODE; episode++)
    {

        time_step = 0;
        current_state = 00;
        goal_state = 30;

        //get all legal actions based on state
        available_actions = env.available_actions(current_state);

        //choose action based on policy
        current_state_idx = env.get_state_index(current_state);
        q_row = env.Q[current_state_idx];
        action = controller.choose_action(epsilon, available_actions, q_row);


        while(1)
        {
            //get next state by taking action
            //next_state = env.take_action(action, current_state);
            //get next state. incorporates transition probs.
            next_state = env.next_state(action, current_state, available_actions);

            //get all legal actions based on state
            available_actions = env.available_actions(next_state);

            //get next action
            next_state_idx = env.get_state_index(next_state);
            q_row = env.Q[next_state_idx];
            next_action = controller.choose_action(epsilon, available_actions, q_row);


            //get reward
            reward = env.get_reward(next_state);

            //TD update
            next_state_idx = env.get_state_index(next_state);
            td_target = reward + discount_factor*env.Q[next_state_idx][next_action];
            td_error = td_target - env.Q[current_state_idx][action];
            env.Q[current_state_idx][action]+= td_error*alpha;

            if (reward == 1000)
            {
                wins++;
                break;
            }
            else if (reward == -1000)
            {
                loses++;
                break;
            }
            else
            {
                time_step++;
                current_state = next_state;
                current_state_idx = env.get_state_index(current_state);
                action = next_action;
            }

            if (episode > 15)
                   {
                    int hey = 0;
                    }
        }
        if (episode % 1 == 0 && episode > 1)
        {
            //print stats
/*            cout<<"-------------------------------------------"<<endl;
            cout<<"Episode number: "<<episode<<" | ";
            cout<<"Time step: "<<time_step<<" | ";
            cout<<"Wins: "<<wins<<" | ";
            cout<<"Loses: "<<loses<<" | ";
            cout<<"Epsilon: "<<epsilon<<endl;*/
            cout<<episode<<","<<time_step<<","<<wins<<","<<loses<<","<<epsilon<<endl;
        }
        //reduce exploration over time and when wins continually increase
        if (episode % 10 == 0 && wins > wins_prev*1.75)
        {
            if (epsilon > 0.06)//because c++ and floats are gay
            {
                epsilon-=0.05;
            }
            else
            {
                epsilon = 0;
            }
        }
    }
}

