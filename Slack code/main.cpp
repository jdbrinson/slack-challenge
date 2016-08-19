//
//  main.cpp
//  Slack code
//
//  Created by Julien Brinson on 8/18/16.
//  Copyright © 2016 Julien Brinson. All rights reserved.
//

#include <iostream>
#include <string>
#include <curl/curl.h>
#include "json/json.h"

#include <pthread.h>

const std::string APIURL = "https://slack.com/api/";
const int CHANNEL_NAME_MAX_LENGTH = 21;
pthread_mutex_t SHUTDOWN;

size_t retrieve_data_callback(char *data, size_t size, size_t nmemb, void *raw_json){
    ((std::string *)raw_json)->append(data, size*nmemb);
    return size*nmemb;
}

void call_slack(std::string slack_token, std::string methodName, std::string &payload, std::string param){
    CURL *curl;
    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, retrieve_data_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &payload);
    std::string request_url = APIURL + methodName +"?token=" + slack_token + param;
    curl_easy_setopt(curl, CURLOPT_URL, request_url.c_str());
    if(curl){
        curl_easy_perform(curl);
    }
    
    curl_easy_cleanup(curl);
}

Json::Value parse_payload(std::string &payload){
    Json::Value jObj;
    Json::Reader reader;
    bool parsingSuccessful = reader.parse(payload.c_str(), jObj);
    if(!parsingSuccessful){
        std::cout<< "failed to parse" << reader.getFormatedErrorMessages();
    }
    return jObj;
}

std::vector<std::string> extract_channels(std::string &payload, std::map<std::string, Json::Value> &j_channelMap){
    Json::Value jObj = parse_payload(payload);
    Json::Value j_channelList = jObj.get("channels", "default");
    std::vector<std::string> channels;
    int channel_index = 0;
    const char* channel_name = "name";
    while(j_channelList.isValidIndex(channel_index)){
        channels.push_back(j_channelList[channel_index][channel_name].asString());
        j_channelMap.insert(std::pair<std::string, Json::Value>(channels[channel_index], j_channelList[channel_index]));
        channel_index++;
    }
    return channels;
}

std::vector<std::string> get_channel_list(std::string slack_token, std::map<std::string, Json::Value> &j_channelMap){
    std::string payload = "";
    std::string methodName = "channels.list";
    std::string param = "";
    call_slack(slack_token, methodName, payload, param);
    std::vector<std::string> channels = extract_channels(payload, j_channelMap);
    std::cout << "Channel List:";
    for(int channel_index = 0; channel_index<channels.size(); channel_index++){
        std::cout <<  "\n" << channel_index+1 << ":" << channels[channel_index];
    }
    std::cout << "\n";
    return channels;
}

bool isMessage(const Json::Value j_message){
    const char* type = "type";
    std::string message = "message";
    return message.compare(j_message[type].asString()) == 0;
}

std::string extract_text(Json::Value j_message){
    const char* text = "text";
    return j_message[text].asString();
}

std::vector<std::string> extract_messages(std::string &payload, std::string channel_id, std::map<std::string, Json::Value> &j_messageMap){
    std::vector<std::string> messages;
    Json::Value jObj = parse_payload(payload);
    const Json::Value j_messageList = jObj.get("messages", "default");
    j_messageMap.insert(std::pair<std::string, Json::Value>(channel_id, j_messageList));
    int messageIndex = 0;
    while(j_messageList.isValidIndex(messageIndex)){
        if(isMessage(j_messageList[messageIndex])){
            messages.push_back(extract_text(j_messageList[messageIndex]));
        }
        messageIndex++;
    }
    return messages;
}


std::string get_channel_id(Json::Value j_channel){
    std::string channel_id = "";
    const char* id = "id";
    channel_id = j_channel[id].asString();
    return channel_id;
}

void show_message_history(std::string slack_token, Json::Value j_channel, std::map<std::string, Json::Value>& j_messageMap){
    
    std::string payload = "";
    std::string methodName = "channels.history";
    std::string channel_id = get_channel_id(j_channel);
    std::string param = "&channel=" + channel_id;
    call_slack(slack_token, methodName, payload, param);
    pthread_mutex_lock(&SHUTDOWN);
    std::vector<std::string> messages = extract_messages(payload, channel_id, j_messageMap);
    pthread_mutex_unlock(&SHUTDOWN);
    std::cout << "Message History: \n";
    for(int message_index = 0; message_index < messages.size(); message_index++){
        std::cout << messages[message_index] << "\n";
    }
}

const void show_message_history_num(std::string slack_token, std::string channel, std::map<std::string, Json::Value> &j_channelMap,std::map<std::string, Json::Value> &j_messageMap ){
    show_message_history(slack_token, j_channelMap[channel], j_messageMap);
}

struct threadData{
    std::string slack_token;
    std::string channel;
    std::map<std::string, Json::Value> j_channelMap;
    std::map<std::string, Json::Value> j_messageMap;
    int num;
};

void* thread_show_history(void* threadarg){
    struct threadData *mydata;
    mydata = (threadData*)threadarg;
    show_message_history_num(mydata->slack_token, mydata->channel, mydata->j_channelMap, mydata->j_messageMap);
    //pthread_exit(NULL);
    return NULL;
}


void select_channel(std::string slack_token, std::vector<std::string> channelList,  std::map<std::string, Json::Value> &j_channelMap, std::map<std::string, Json::Value> &j_messageMap){
    void * status;
    char selected[CHANNEL_NAME_MAX_LENGTH];
    
    std::cout << "select channel (number or name) for message history: ";
    std::cin >> selected;
    if(isdigit(selected[0])){
        int num;
        //converts string to int
        std::stringstream convert(selected);
        if(!(convert>> num)){
            num = 1;
        }
        num--;
        std::string channel = channelList[num];
        pthread_t threads[1];
        struct threadData td;
        td.slack_token = slack_token;
        td.channel = channelList[num];
        td.j_channelMap = j_channelMap;
        int rc = pthread_create(&threads[0], NULL, thread_show_history, (void*)&td);
        if(rc){
            std::cout << "couldn't create thread";
            exit(-1);
        }
        
        for(int i = 0; i <channelList.size(); i++){
            if(i != num){
                show_message_history_num(slack_token, channelList[i], j_channelMap, j_messageMap);
            }
           
        }
        pthread_join(threads[0], &status);
        
    }else{
        show_message_history(slack_token, j_channelMap[selected], j_messageMap);
    }
}

int main(int argc, const char * argv[]) {
    // insert code here...
    /*Pseudo code to implement:
     1)intro output
     */
    std::string slack_token;
    std::cout << "Welcome to Slack!\n";
    std::cout << "Please provide token: ";
    
    std::cin >> slack_token;
    std::map<std::string, Json::Value> j_channelMap;
    std::map<std::string, Json::Value> j_messageMap;
     //2)get/list all available channels
    std::vector<std::string> channelList = get_channel_list(slack_token, j_channelMap);
     //3)wait for select channel/show message history
    select_channel(slack_token, channelList, j_channelMap, j_messageMap);
//    std::cout << "final shabang!\n";
//    for(std::map<std::string, Json::Value>::iterator it=j_messageMap.begin(); it!=j_messageMap.end(); ++it){
//        std::cout << it->first << " : " << it->second << "\n";
//    }
    
     //4)cache data
    
    pthread_mutex_destroy(&SHUTDOWN);
    pthread_exit(NULL);

    return 0;
}
