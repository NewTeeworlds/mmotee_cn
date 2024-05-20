from jsonwriter import writejsonsplited
def splitkey(name):
    fo=open(name,'r')
    re=fo.readlines()
    fo.close()
    re=list(filter(lambda x:('key' in x),re))
    for i in range(len(re)):
        re[i]=re[i].strip(' ')
        for j in ['\t','\n','"key":','"']:
            re[i]=re[i].replace(j,'')
    return re
def splitvalue(name):
    fo=open(name,'r',encoding='utf-8')
    re=fo.readlines()
    fo.close()
    re=list(filter(lambda x:('value' in x),re))
    for i in range(len(re)):
        re[i]=re[i].strip(' ')
        for j in ['\t','\n','"value":','"']:
            re[i]=re[i].replace(j,'')
    return re
def justprint(name):
    fo=open(name,'r',encoding='utf-8')
    re=fo.readlines()
    fo.close()
    print(re)

if __name__=='__main__':
    w=splitkey('orginal.txt')
    fw=open('keys.txt','w')
    fw.write('\n'.join(w))
    fw.close()
        
