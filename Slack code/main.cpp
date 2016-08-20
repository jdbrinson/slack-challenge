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
#include <fstream>
#include <queue>

const std::string APIURL = "https://slack.com/api/";
const int CHANNEL_NAME_MAX_LENGTH = 21;
pthread_mutex_t SHUTDOWN;
const int BUFFER_SIZE = 2000;

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
    bool parsingSuccessful = reader.parse(payload, jObj);
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
        channels.push_back(j_channelList[channel_index][channel_name].toStyledString());
        j_channelMap.insert(std::pair<std::string, Json::Value>(channels[channel_index], j_channelList[channel_index]));
        channel_index++;
    }
    return channels;
}

void cache_upkeep(const std::string content, std::map<std::string, Json::Value> &j_objMap, std::string channel_id){
    //create_cache if none
    std::queue<std::string> cache_update;
    std::string file_path = "cache_"+ content + ".txt";
    char j_buffer[BUFFER_SIZE];
    std::fstream cache;
    cache.open(file_path, std::fstream::in|std::fstream::trunc);
    cache.getline(j_buffer, BUFFER_SIZE);
    cache.close();
    if(std::strlen(j_buffer) == 0){ //new file because it's empty
        
        for(std::map<std::string, Json::Value>::iterator it = j_objMap.begin(); it != j_objMap.end(); ++it){
            std::string map_payload = it->first + ":"+ it->second.toStyledString() + "\n";
            
//            int channel_index =0;
//            while(it->second.isValidIndex(channel_index)){
//                map_payload += it->second[channel_index].asString();
//                channel_index++;
//            }
            cache_update.push(map_payload);
            //+ scribe->write(it->second) + "\n";
        }
    }else{
        cache.open(file_path, std::fstream::in);
        do{
            size_t num_key_digits = std::strcspn(j_buffer, ":");
            char old_key[num_key_digits+1];
            strncpy(old_key, j_buffer, num_key_digits);
            old_key[num_key_digits+1] = '\n';
            if(channel_id.compare(old_key) == 0 && content.compare("messages")==0){
                try{
                    j_objMap.at(old_key);
                }
                catch(const std::out_of_range &oor){
                    //found nothing/out of date
                    
                } //was found/not out of date
                char *json = j_buffer + num_key_digits + 1;
                if(strcmp(json, j_objMap[old_key].asCString()) != 0 ){
                    std::string key(old_key);
                    std::string map_payload = key + ":" + j_objMap[key].toStyledString() + "\n";
                    cache_update.push(map_payload);
                }
                cache_update.push(j_buffer);
            }else{
                cache_update.push(j_buffer);
            }
        }while(cache.getline(j_buffer, BUFFER_SIZE));
        cache.close();
        
    }
    cache.open(file_path, std::fstream::out|std::fstream::app);
    while(!cache_update.empty()){
        std::string map_payload = cache_update.front();
        cache.write(map_payload.c_str(), map_payload.length());
        cache_update.pop();
    }
    cache.close();
    // else check if content is updated and if not update contents
}


std::vector<std::string> cache_channel_list(std::string slack_token, std::map<std::string, Json::Value> &j_channelMap){
    
    std::ofstream channel_cache;
    channel_cache.open("cache/channels.txt");
    
    std::string payload = "";
    std::string methodName = "channels.list";
    std::string param = "";
    call_slack(slack_token, methodName, payload, param);
    std::vector<std::string> channels = extract_channels(payload, j_channelMap);
    cache_upkeep("channels", j_channelMap, "");
    
    return channels;
    
}

std::vector<std::string> get_channel_list(std::string slack_token, std::map<std::string, Json::Value> &j_channelMap){
    std::vector<std::string> channels = cache_channel_list(slack_token, j_channelMap);
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
    return message.compare(j_message[type].toStyledString()) == 0;
}

std::string extract_text(Json::Value j_message){
    const char* text = "text";
    return j_message[text].toStyledString();
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
    channel_id = j_channel[id].toStyledString();
    return channel_id;
}

std::vector<std::string> cache_message_history(std::string slack_token, Json::Value j_channel, std::map<std::string, Json::Value>& j_messageMap){
    std::string payload = "";
    std::string methodName = "channels.history";
    std::string channel_id = get_channel_id(j_channel);
    std::string param = "&channel=" + channel_id;
    call_slack(slack_token, methodName, payload, param);
    pthread_mutex_lock(&SHUTDOWN);
    std::vector<std::string> messages = extract_messages(payload, channel_id, j_messageMap);
    cache_upkeep("messages", j_messageMap, channel_id);
    pthread_mutex_unlock(&SHUTDOWN);
    return messages;

}

void cache_message_history_num(std::string slack_token, std::string channel, std::map<std::string, Json::Value> &j_channelMap, std::map<std::string, Json::Value> &j_messageMap ){
        cache_message_history(slack_token, j_channelMap[channel], j_messageMap);
}

void show_message_history(std::string slack_token, Json::Value j_channel, std::map<std::string, Json::Value>& j_messageMap){
    std::vector<std::string> messages = cache_message_history(slack_token, j_channel, j_messageMap);
    std::cout << j_channel["name"].toStyledString()<<" - Message History: \n";
    for(int message_index = 0; message_index < messages.size(); message_index++){
        std::cout << messages[message_index] << "\n";
    }
}

const void show_message_history_num(std::string slack_token, std::string channel, std::map<std::string, Json::Value> &j_channelMap, std::map<std::string, Json::Value> &j_messageMap ){
    show_message_history(slack_token, j_channelMap[channel], j_messageMap);
}

struct threadData{
    std::string slack_token;
    std::string channel_name;
    Json::Value channel;
    std::map<std::string, Json::Value> *j_channelMap_ptr;
    std::map<std::string, Json::Value> *j_messageMap_ptr;
    int num;
};

void* thread_show_history_num(void* threadarg){
    struct threadData *mydata;
    mydata = (threadData*)threadarg;
    show_message_history_num(mydata->slack_token, mydata->channel_name, *(mydata->j_channelMap_ptr), *(mydata->j_messageMap_ptr));
    pthread_exit(NULL);
    return NULL;
}

void* thread_show_history(void* threadarg){
    struct threadData *mydata;
    mydata = (threadData*)threadarg;
    show_message_history(mydata->slack_token, mydata->channel, *(mydata->j_messageMap_ptr));
    pthread_exit(NULL);
    return NULL;
}

void select_channel(std::string slack_token, std::vector<std::string> channelList,  std::map<std::string, Json::Value> &j_channelMap, std::map<std::string, Json::Value> &j_messageMap){
    void * status;
    char selected[CHANNEL_NAME_MAX_LENGTH];
    
    std::cout << "select channel (number or name) for message history: ";
    std::cin >> selected;
    pthread_t threads[1];
    struct threadData td;
    td.slack_token = slack_token;
    
    td.j_channelMap_ptr = &j_channelMap;
    td.j_messageMap_ptr = &j_messageMap;
    if(isdigit(selected[0])){
        int num;
        //converts string to int
        std::stringstream convert(selected);
        if(!(convert>> num)){
            num = 1;
        }
        num--;
        td.channel_name = channelList[num];
        int rc = pthread_create(&threads[0], NULL, thread_show_history_num, (void*)&td);
        if(rc){
            std::cout << "couldn't create thread";
            exit(-1);
        }
        
        for(int i = 0; i <channelList.size(); i++){
            if(i != num){
                cache_message_history_num(slack_token, channelList[i], j_channelMap, j_messageMap);
            }
           
        }
        
        
    }else{
        td.channel = j_channelMap[selected];
        int rc = pthread_create(&threads[0], NULL, thread_show_history, (void*)&td);
        if(rc){
            std::cout << "couldn't create thread";
            exit(-1);
        }
        for(std::map<std::string, Json::Value>::iterator it = j_channelMap.begin(); it != j_channelMap.end(); ++it){
            if(std::strcmp(selected, it->first.c_str()) != 0){
                cache_message_history(slack_token, j_channelMap[it->first.c_str()], j_messageMap);
            }
        }
        
    }
    pthread_join(threads[0], &status);
}

int main(int argc, const char * argv[]) {
    // insert code here...
    /*Pseudo code to implement:
     1)intro output
     */
    pthread_mutex_init(&SHUTDOWN, NULL);
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
//    
     //4)cache data
    
    pthread_mutex_destroy(&SHUTDOWN);
    //pthread_exit(NULL);

    return 0;
}
